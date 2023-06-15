#include "Optimizations.hpp"

#include <polyfem/mesh/GeometryReader.hpp>
#include <jse/jse.h>
#include <polyfem/State.hpp>

#include "AdjointNLProblem.hpp"
#include "LBFGSBSolver.hpp"
#include "LBFGSSolver.hpp"
#include "BFGSSolver.hpp"
#include "MMASolver.hpp"
#include "GradientDescentSolver.hpp"

#include <polyfem/solver/forms/adjoint_forms/TargetForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/SumCompositeForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/CompositeForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/TransientForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/SmoothingForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/AMIPSForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/BarrierForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/TractionNormForm.hpp>

#include <polyfem/solver/forms/parametrization/Parametrizations.hpp>
#include <polyfem/solver/forms/parametrization/SDFParametrizations.hpp>
#include <polyfem/solver/forms/parametrization/NodeCompositeParametrizations.hpp>
#include <polyfem/solver/forms/parametrization/SplineParametrizations.hpp>

#include <polyfem/solver/forms/adjoint_forms/ParametrizedProductForm.hpp>

#include <polyfem/io/OBJReader.hpp>

namespace polyfem::solver
{
	namespace
	{
		bool load_json(const std::string &json_file, json &out)
		{
			std::ifstream file(json_file);

			if (!file.is_open())
				return false;

			file >> out;

			out["root_path"] = json_file;

			return true;
		}
	} // namespace

	std::shared_ptr<cppoptlib::NonlinearSolver<AdjointNLProblem>> make_nl_solver(const json &solver_params)
	{
		const std::string name = solver_params["solver"].template get<std::string>();
		if (name == "GradientDescent" || name == "gradientdescent" || name == "gradient")
		{
			return std::make_shared<cppoptlib::GradientDescentSolver<AdjointNLProblem>>(
				solver_params, 0.);
		}
		else if (name == "lbfgs" || name == "LBFGS" || name == "L-BFGS")
		{
			return std::make_shared<cppoptlib::LBFGSSolver<AdjointNLProblem>>(
				solver_params, 0.);
		}
		else if (name == "bfgs" || name == "BFGS" || name == "BFGS")
		{
			return std::make_shared<cppoptlib::BFGSSolver<AdjointNLProblem>>(
				solver_params, 0.);
		}
		else if (name == "lbfgsb" || name == "LBFGSB" || name == "L-BFGS-B")
		{
			return std::make_shared<cppoptlib::LBFGSBSolver<AdjointNLProblem>>(
				solver_params, 0.);
		}
		else if (name == "mma" || name == "MMA")
		{
			return std::make_shared<cppoptlib::MMASolver<AdjointNLProblem>>(
				solver_params, 0.);
		}
		else
		{
			throw std::invalid_argument(fmt::format("invalid nonlinear solver type: {}", name));
		}
	}

	std::shared_ptr<AdjointForm> create_form(const json &args, const std::vector<std::shared_ptr<VariableToSimulation>> &var2sim, const std::vector<std::shared_ptr<State>> &states)
	{
		std::shared_ptr<AdjointForm> obj;
		if (args.is_array())
		{
			std::vector<std::shared_ptr<AdjointForm>> forms;
			for (const auto &arg : args)
				forms.push_back(create_form(arg, var2sim, states));

			obj = std::make_shared<SumCompositeForm>(var2sim, forms);
		}
		else
		{
			const std::string type = args["type"];
			if (type == "transient_integral")
			{
				std::shared_ptr<StaticForm> static_obj = std::dynamic_pointer_cast<StaticForm>(create_form(args["static_objective"], var2sim, states));
				const auto &state = states[args["state"]];
				obj = std::make_shared<TransientForm>(var2sim, state->args["time"]["time_steps"], state->args["time"]["dt"], args["integral_type"], args["steps"].get<std::vector<int>>(), static_obj);
			}
			else if (type == "power")
			{
				std::shared_ptr<AdjointForm> obj_aux = create_form(args["objective"], var2sim, states);
				obj = std::make_shared<PowerForm>(obj_aux, args["power"]);
			}
			else if (type == "divide")
			{
				std::shared_ptr<AdjointForm> obj1 = create_form(args["objective"][0], var2sim, states);
				std::shared_ptr<AdjointForm> obj2 = create_form(args["objective"][1], var2sim, states);
				std::vector<std::shared_ptr<AdjointForm>> objs({obj1, obj2});
				obj = std::make_shared<DivideForm>(objs);
			}
			else if (type == "plus-const")
			{
				obj = std::make_shared<PlusConstCompositeForm>(create_form(args["objective"], var2sim, states), args["value"]);
			}
			else if (type == "compliance")
			{
				obj = std::make_shared<ComplianceForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "acceleration")
			{
				obj = std::make_shared<AccelerationForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "kinetic")
			{
				obj = std::make_shared<AccelerationForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "target")
			{
				std::shared_ptr<TargetForm> tmp = std::make_shared<TargetForm>(var2sim, *(states[args["state"]]), args);
				auto reference_cached = args["reference_cached_body_ids"].get<std::vector<int>>();
				tmp->set_reference(states[args["target_state"]], std::set(reference_cached.begin(), reference_cached.end()));
				obj = tmp;
			}
			else if (type == "center-target")
			{
				obj = std::make_shared<BarycenterTargetForm>(var2sim, args, states[args["state"]], states[args["target_state"]]);
			}
			else if (type == "sdf-target")
			{
				std::shared_ptr<SDFTargetForm> tmp = std::make_shared<SDFTargetForm>(var2sim, *(states[args["state"]]), args);
				double delta = args["delta"].get<double>();
				if (!states[args["state"]]->mesh->is_volume())
				{
					int dim = 2;
					Eigen::MatrixXd control_points(args["control_points"].size(), dim);
					for (int i = 0; i < control_points.rows(); ++i)
						for (int j = 0; j < control_points.cols(); ++j)
							control_points(i, j) = args["control_points"][i][j].get<double>();
					Eigen::VectorXd knots(args["knots"].size());
					for (int i = 0; i < knots.size(); ++i)
						knots(i) = args["knots"][i].get<double>();
					tmp->set_bspline_target(control_points, knots, delta);
				}
				else
				{
					int dim = 3;
					Eigen::MatrixXd control_points_grid(args["control_points_grid"].size(), dim);
					for (int i = 0; i < control_points_grid.rows(); ++i)
						for (int j = 0; j < control_points_grid.cols(); ++j)
							control_points_grid(i, j) = args["control_points_grid"][i][j].get<double>();
					Eigen::VectorXd knots_u(args["knots_u"].size());
					for (int i = 0; i < knots_u.size(); ++i)
						knots_u(i) = args["knots_u"][i].get<double>();
					Eigen::VectorXd knots_v(args["knots_v"].size());
					for (int i = 0; i < knots_v.size(); ++i)
						knots_v(i) = args["knots_v"][i].get<double>();
					tmp->set_bspline_target(control_points_grid, knots_u, knots_v, delta);
				}

				obj = tmp;
			}
			else if (type == "mesh-target")
			{
				std::shared_ptr<MeshTargetForm> tmp = std::make_shared<MeshTargetForm>(var2sim, *(states[args["state"]]), args);
				double delta = args["delta"].get<double>();
				Eigen::MatrixXd V;
				Eigen::MatrixXi E, F;
				bool read = polyfem::io::OBJReader::read(args["mesh_path"], V, E, F);
				if (!read)
					log_and_throw_error(fmt::format("Could not read mesh! {}", args["mesh"]));
				tmp->set_surface_mesh_target(V, F, delta);
				obj = tmp;
			}
			else if (type == "function-target")
			{
				std::shared_ptr<TargetForm> tmp = std::make_shared<TargetForm>(var2sim, *(states[args["state"]]), args);
				tmp->set_reference(args["target_function"], args["target_function_gradient"]);
				obj = tmp;
			}
			else if (type == "position")
			{
				obj = std::make_shared<PositionForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "stress")
			{
				obj = std::make_shared<StressForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "disp_grad")
			{
				obj = std::make_shared<DispGradForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "homo_disp_grad")
			{
				obj = std::make_shared<HomogenizedDispGradForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "stress_norm")
			{
				obj = std::make_shared<StressNormForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "elastic_energy")
			{
				obj = std::make_shared<ElasticEnergyForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "traction_norm")
			{
				obj = std::make_shared<TractionNormForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "max_stress")
			{
				obj = std::make_shared<MaxStressForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "weighted_solution")
			{
				obj = std::make_shared<WeightedSolution>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "volume")
			{
				obj = std::make_shared<VolumeForm>(var2sim, *(states[args["state"]]), args);
			}
			else if (type == "soft_constraint")
			{
				std::vector<std::shared_ptr<AdjointForm>> forms({create_form(args["objective"], var2sim, states)});
				Eigen::VectorXd bounds;
				nlohmann::adl_serializer<Eigen::VectorXd>::from_json(args["soft_bound"], bounds);
				obj = std::make_shared<InequalityConstraintForm>(forms, bounds, args["power"]);
			}
			else if (type == "AMIPS")
			{
				obj = std::make_shared<AMIPSForm>(var2sim, *(states[args["state"]]));
			}
			else if (type == "collision_barrier")
			{
				obj = std::make_shared<CollisionBarrierForm>(var2sim, *(states[args["state"]]), args["dhat"]);
			}
			else if (type == "parametrized_product")
			{
				if (args["parametrization"].contains("parameter_index"))
					log_and_throw_error("Parametrizations in parametrized forms don't support parameter_index!");

				std::vector<std::shared_ptr<Parametrization>> map_list;
				for (const auto &arg : args["parametrization"])
					map_list.push_back(create_parametrization(arg, states, {}));
				CompositeParametrization composite_map = CompositeParametrization(map_list);

				obj = std::make_shared<ParametrizedProductForm>(composite_map);
			}
			else
				log_and_throw_error("Objective not implemented!");

			obj->set_weight(args["weight"]);
			if (args["print_energy"].get<std::string>() != "")
				obj->enable_energy_print(args["print_energy"]);
		}

		return obj;
	}

	std::shared_ptr<Parametrization> create_parametrization(const json &args, const std::vector<std::shared_ptr<State>> &states, const std::vector<int> &variable_sizes)
	{
		std::shared_ptr<Parametrization> map;
		const std::string type = args["type"];
		if (type == "per-body-to-per-elem")
		{
			map = std::make_shared<PerBody2PerElem>(*(states[args["state"]]->mesh));
		}
		else if (type == "per-body-to-per-node")
		{
			map = std::make_shared<PerBody2PerNode>(*(states[args["state"]]->mesh), states[args["state"]]->bases, states[args["state"]]->n_bases);
		}
		else if (type == "E-nu-to-lambda-mu")
		{
			map = std::make_shared<ENu2LambdaMu>(args["is_volume"]);
		}
		else if (type == "slice")
		{
			if (args.contains("from") && args.contains("to"))
			{
				assert(args["from"] != -1);
				assert(args["to"] != -1);
				map = std::make_shared<SliceMap>(args["from"], args["to"], args["last"]);
			}
			else if (args.contains("parameter_index"))
			{
				assert(args["parameter_index"] != -1);
				int idx = args["parameter_index"].get<int>();
				int from, to, last;
				int cumulative = 0;
				for (int i = 0; i < variable_sizes.size(); ++i)
				{
					if (i == idx)
					{
						from = cumulative;
						to = from + variable_sizes[i];
					}
					cumulative += variable_sizes[i];
				}
				last = cumulative;
				map = std::make_shared<SliceMap>(from, to, last);
			}
			else
				log_and_throw_error("Incorrect spec for SliceMap!");
		}
		else if (type == "exp")
		{
			map = std::make_shared<ExponentialMap>();
		}
		else if (type == "scale")
		{
			map = std::make_shared<Scaling>(args["value"]);
		}
		else if (type == "power")
		{
			map = std::make_shared<PowerMap>(args["power"]);
		}
		else if (type == "append-values")
		{
			Eigen::VectorXd vals;
			nlohmann::adl_serializer<Eigen::VectorXd>::from_json(args["values"], vals);
			map = std::make_shared<InsertConstantMap>(vals);
		}
		else if (type == "append-const")
		{
			map = std::make_shared<InsertConstantMap>(args["size"], args["value"], args["start"]);
		}
		else if (type == "linear-filter")
		{
			map = std::make_shared<LinearFilter>(*(states[args["state"]]->mesh), args["radius"]);
		}
		else if (type == "custom-symmetric")
		{
			map = std::make_shared<CustomSymmetric>(args);
		}
		else if (type == "sdf-to-mesh")
		{
			map = std::make_shared<SDF2Mesh>(args["binary_path"], args["wire_path"], args["output_path"], args["use_volume_velocity"], args["options"]);
		}
		else if (type == "periodic-mesh-tile")
		{
			Eigen::VectorXi dims;
			nlohmann::adl_serializer<Eigen::VectorXi>::from_json(args["dimensions"], dims);
			map = std::make_shared<MeshTiling>(dims, args["input_path"], args["output_path"]);
		}
		else if (type == "mesh-affine")
		{
			MatrixNd A;
			VectorNd b;
			mesh::construct_affine_transformation(
				args["transformation"],
				VectorNd::Ones(args["dimension"]),
				A, b);
			map = std::make_shared<MeshAffine>(A, b, args["input_path"], args["output_path"]);
		}
		else if (type == "bounded-biharmonic-weights")
		{
			map = std::make_shared<BoundedBiharmonicWeights2Dto3D>(args["num_control_vertices"], args["num_vertices"], *states[args["state"]], args["allow_rotations"]);
		}
		else
			log_and_throw_error("Unkown parametrization!");

		return map;
	}

	std::shared_ptr<VariableToSimulation> create_variable_to_simulation(const json &args, const std::vector<std::shared_ptr<State>> &states, const std::vector<int> &variable_sizes)
	{
		std::shared_ptr<VariableToSimulation> var2sim;
		const std::string type = args["type"];

		std::vector<std::shared_ptr<Parametrization>> map_list;
		for (const auto &arg : args["composition"])
			map_list.push_back(create_parametrization(arg, states, variable_sizes));
		CompositeParametrization composite_map;

		std::vector<std::shared_ptr<State>> cur_states;
		if (args["state"].is_array())
			for (int i : args["state"])
				cur_states.push_back(states[i]);
		else
			cur_states.push_back(states[args["state"]]);

		composite_map = CompositeParametrization(map_list);

		const std::string composite_map_type = args["composite_map_type"];
		Eigen::VectorXi output_indexing;
		if (composite_map_type == "none")
		{
		}
		else if (composite_map_type == "interior")
		{
			assert(type == "shape");
			VariableToInteriorNodes variable_to_node(*cur_states[0], args["volume_selection"][0]);
			output_indexing = variable_to_node.get_output_indexing();
		}
		else if (composite_map_type == "boundary")
		{
			assert(type == "shape");
			VariableToBoundaryNodes variable_to_node(*cur_states[0], args["surface_selection"][0]);
			output_indexing = variable_to_node.get_output_indexing();
		}
		else if (composite_map_type == "boundary_excluding_surface")
		{
			assert(type == "shape");
			std::vector<int> excluded_surfaces;
			for (const auto &s : args["surface_selection"])
				excluded_surfaces.push_back(s);
			VariableToBoundaryNodesExclusive variable_to_node(*cur_states[0], excluded_surfaces);
			output_indexing = variable_to_node.get_output_indexing();
		}

		if (type == "shape")
		{
			var2sim = std::make_shared<ShapeVariableToSimulation>(cur_states, composite_map);
			var2sim->set_output_indexing(output_indexing);
		}
		else if (type == "elastic")
		{
			var2sim = std::make_shared<ElasticVariableToSimulation>(cur_states, composite_map);
		}
		else if (type == "friction")
		{
			var2sim = std::make_shared<FrictionCoeffientVariableToSimulation>(cur_states, composite_map);
		}
		else if (type == "damping")
		{
			var2sim = std::make_shared<DampingCoeffientVariableToSimulation>(cur_states, composite_map);
		}
		else if (type == "macro-strain")
		{
			var2sim = std::make_shared<MacroStrainVariableToSimulation>(cur_states, composite_map);
		}
		else if (type == "initial")
		{
			var2sim = std::make_shared<InitialConditionVariableToSimulation>(cur_states, composite_map);
		}
		else if (type == "sdf-shape")
		{
			var2sim = std::make_shared<SDFShapeVariableToSimulation>(cur_states, composite_map, args);
		}
		else if (type == "sdf-periodic-shape")
		{
			var2sim = std::make_shared<SDFPeriodicShapeVariableToSimulation>(cur_states, composite_map, args);
		}
		else if (type == "periodic-shape-scale")
		{
			var2sim = std::make_shared<PeriodicShapeScaleVariableToSimulation>(cur_states, composite_map, args);
		}
		else if (type == "periodic-shape")
		{
			var2sim = std::make_shared<PeriodicShapeVariableToSimulation>(cur_states, composite_map);
		}
		else if (type == "dirichlet")
		{
			var2sim = std::make_shared<DirichletVariableToSimulation>(cur_states, composite_map);
		}

		return var2sim;
	}

	std::shared_ptr<State> create_state(const json &args, const int max_threads)
	{
		std::shared_ptr<State> state = std::make_shared<State>();
		state->set_max_threads(max_threads);

		json in_args = args;
		in_args["solver"]["max_threads"] = max_threads;
		if (!args.contains("output") || !args["output"].contains("log") || !args["output"]["log"].contains("level"))
		{
			auto tmp = R"({
					"output": {
						"log": {
							"level": -1
						}
					}
				})"_json;

			tmp["output"]["log"]["level"] = "error";

			in_args.merge_patch(tmp);
		}

		in_args["optimization"]["enabled"] = true;
		state->init(in_args, false);
		state->load_mesh();
		Eigen::MatrixXd sol, pressure;
		state->build_basis();
		state->assemble_rhs();
		state->assemble_mass_mat();

		return state;
	}

	void solve_pde(State &state)
	{
		state.assemble_rhs();
		state.assemble_mass_mat();
		Eigen::MatrixXd sol, pressure;
		state.solve_problem(sol, pressure);
	}

	void apply_objective_json_spec(json &args, const json &rules)
	{
		if (args.is_array())
		{
			for (auto &arg : args)
				apply_objective_json_spec(arg, rules);
		}
		else if (args.is_object())
		{
			jse::JSE jse;
			const bool valid_input = jse.verify_json(args, rules);

			if (!valid_input)
			{
				logger().error("invalid objective json:\n{}", jse.log2str());
				throw std::runtime_error("Invald objective json file");
			}

			args = jse.inject_defaults(args, rules);

			for (auto &it : args.items())
			{
				if (it.key().find("objective") != std::string::npos)
					apply_objective_json_spec(it.value(), rules);
			}
		}
	}

	json apply_opt_json_spec(const json &input_args, bool strict_validation)
	{
		json args_in = input_args;

		// CHECK validity json
		json rules;
		jse::JSE jse;
		{
			jse.strict = strict_validation;
			const std::string polyfem_input_spec = POLYFEM_OPT_INPUT_SPEC;
			std::ifstream file(polyfem_input_spec);

			if (file.is_open())
				file >> rules;
			else
			{
				logger().error("unable to open {} rules", polyfem_input_spec);
				throw std::runtime_error("Invald spec file");
			}
		}

		const bool valid_input = jse.verify_json(args_in, rules);

		if (!valid_input)
		{
			logger().error("invalid input json:\n{}", jse.log2str());
			throw std::runtime_error("Invald input json file");
		}

		json args = jse.inject_defaults(args_in, rules);

		json obj_rules;
		{
			const std::string polyfem_objective_spec = POLYFEM_OBJECTIVE_INPUT_SPEC;
			std::ifstream file(polyfem_objective_spec);

			if (file.is_open())
				file >> obj_rules;
			else
			{
				logger().error("unable to open {} rules", polyfem_objective_spec);
				throw std::runtime_error("Invald spec file");
			}
		}
		apply_objective_json_spec(args["functionals"], obj_rules);

		if (args.contains("stopping_conditions"))
			apply_objective_json_spec(args["stopping_conditions"], obj_rules);

		return args;
	}

	int compute_variable_size(const json &args, const std::vector<std::shared_ptr<State>> &states)
	{
		if (args["number"].is_number())
		{
			return args["number"].get<int>();
		}
		else if (args["number"].is_null() && args["initial"].size() > 0)
		{
			return args["initial"].size();
		}
		else if (args["number"].is_object())
		{
			auto selection = args["number"];
			if (selection.contains("surface_selection"))
			{
				auto surface_selection = selection["surface_selection"].get<std::vector<int>>();
				auto state_id = selection["state"];
				std::set<int> node_ids = {};
				for (const auto &surface : surface_selection)
				{
					std::vector<int> ids;
					states[state_id]->compute_surface_node_ids(surface, ids);
					for (const auto &i : ids)
						node_ids.insert(i);
				}
				return node_ids.size() * states[state_id]->mesh->dimension();
			}
			else if (selection.contains("volume_selection"))
			{
				auto volume_selection = selection["volume_selection"].get<std::vector<int>>();
				auto state_id = selection["state"];
				std::set<int> node_ids = {};
				for (const auto &volume : volume_selection)
				{
					std::vector<int> ids;
					states[state_id]->compute_volume_node_ids(volume, ids);
					for (const auto &i : ids)
						node_ids.insert(i);
				}

				if (selection["exclude_boundary_nodes"])
				{
					std::vector<int> ids;
					states[state_id]->compute_total_surface_node_ids(ids);
					for (const auto &i : ids)
						node_ids.erase(i);
				}

				return node_ids.size() * states[state_id]->mesh->dimension();
			}
			else
			{
				log_and_throw_error("Incorrect specification for parameters.");
			}
		}
		else
			log_and_throw_error("Incorrect specification for parameters.");

		return -1;
	}

} // namespace polyfem::solver