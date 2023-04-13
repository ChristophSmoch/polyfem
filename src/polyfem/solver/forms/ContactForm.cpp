#include "ContactForm.hpp"

#include <polyfem/solver/NLProblem.hpp>
#include <polyfem/solver/forms/FrictionForm.hpp>
#include <polyfem/utils/Types.hpp>
#include <polyfem/utils/Timer.hpp>
#include <polyfem/utils/Logger.hpp>
#include <polyfem/utils/MatrixUtils.hpp>
#include <polyfem/utils/MaybeParallelFor.hpp>

#include <polyfem/io/OBJWriter.hpp>

#include <ipc/barrier/adaptive_stiffness.hpp>
#include <ipc/utils/world_bbox_diagonal_length.hpp>

#include <igl/writePLY.h>

namespace polyfem::solver
{
	ContactForm::ContactForm(const ipc::CollisionMesh &collision_mesh,
							 const double dhat,
							 const double avg_mass,
							 const bool use_convergent_formulation,
							 const bool use_adaptive_barrier_stiffness,
							 const bool is_time_dependent,
							 const ipc::BroadPhaseMethod broad_phase_method,
							 const double ccd_tolerance,
							 const int ccd_max_iterations)
		: collision_mesh_(collision_mesh),
		  dhat_(dhat),
		  avg_mass_(avg_mass),
		  use_adaptive_barrier_stiffness_(use_adaptive_barrier_stiffness),
		  is_time_dependent_(is_time_dependent),
		  broad_phase_method_(broad_phase_method),
		  ccd_tolerance_(ccd_tolerance),
		  ccd_max_iterations_(ccd_max_iterations)
	{
		assert(dhat_ > 0);
		assert(ccd_tolerance > 0);

		prev_distance_ = -1;
		constraint_set_.set_use_convergent_formulation(use_convergent_formulation);
	}

	void ContactForm::init(const Eigen::VectorXd &x)
	{
		update_constraint_set(compute_displaced_surface(x));
	}

	void ContactForm::update_quantities(const double t, const Eigen::VectorXd &x)
	{
		update_constraint_set(compute_displaced_surface(x));
	}

	Eigen::MatrixXd ContactForm::compute_displaced_surface(const Eigen::VectorXd &x) const
	{
		return collision_mesh_.displace_vertices(utils::unflatten(x, collision_mesh_.dim()));
	}

	void ContactForm::update_barrier_stiffness(const Eigen::VectorXd &x, const Eigen::MatrixXd &grad_energy)
	{
		if (!use_adaptive_barrier_stiffness())
			return;

		const Eigen::MatrixXd displaced_surface = compute_displaced_surface(x);

		// The adative stiffness is designed for the non-convergent formulation,
		// so we need to compute the gradient of the non-convergent barrier.
		// After we can map it to a good value for the convergent formulation.
		ipc::CollisionConstraints nonconvergent_constraints;
		nonconvergent_constraints.set_use_convergent_formulation(false);
		nonconvergent_constraints.build(
			collision_mesh_, displaced_surface, dhat_, dmin_, broad_phase_method_);
		Eigen::VectorXd grad_barrier = nonconvergent_constraints.compute_potential_gradient(
			collision_mesh_, displaced_surface, dhat_);
		grad_barrier = collision_mesh_.to_full_dof(grad_barrier);

		weight_ = ipc::initial_barrier_stiffness(
			ipc::world_bbox_diagonal_length(displaced_surface), dhat_, avg_mass_,
			grad_energy, grad_barrier, max_barrier_stiffness_);
		if (use_convergent_formulation())
		{
			double scaling_factor = 0;
			if (!nonconvergent_constraints.empty())
			{
				const double nonconvergent_potential = nonconvergent_constraints.compute_potential(
					collision_mesh_, displaced_surface, dhat_);

				update_constraint_set(displaced_surface);
				// convergent_constraints = constraint_set_;
				const double convergent_potential = constraint_set_.compute_potential(
					collision_mesh_, displaced_surface, dhat_);

				scaling_factor = nonconvergent_potential / convergent_potential;
			}
			else
			{
				scaling_factor = dhat_ * std::pow(dhat_ + 2 * dmin_, 2);
			}
			weight_ *= scaling_factor;
			max_barrier_stiffness_ *= scaling_factor;
		}

		logger().debug("adaptive barrier form stiffness {}", barrier_stiffness());

		// Eigen::VectorXd tmp;
		// this->first_derivative(x, tmp);
		// logger().debug("grad_energy {}; grad_barrier {}", grad_energy.norm(), tmp.norm());
	}

	void ContactForm::update_constraint_set(const Eigen::MatrixXd &displaced_surface)
	{
		// Store the previous value used to compute the constraint set to avoid duplicate computation.
		static Eigen::MatrixXd cached_displaced_surface;
		if (cached_displaced_surface.size() == displaced_surface.size() && cached_displaced_surface == displaced_surface)
			return;

		if (use_cached_candidates_)
			constraint_set_.build(
				candidates_, collision_mesh_, displaced_surface, dhat_);
		else
			constraint_set_.build(
				collision_mesh_, displaced_surface, dhat_, dmin_, broad_phase_method_);
		cached_displaced_surface = displaced_surface;
	}

	double ContactForm::value_unweighted(const Eigen::VectorXd &x) const
	{
		return constraint_set_.compute_potential(collision_mesh_, compute_displaced_surface(x), dhat_);
	}

	Eigen::VectorXd ContactForm::value_per_element_unweighted(const Eigen::VectorXd &x) const
	{
		const Eigen::MatrixXd V = compute_displaced_surface(x);
		assert(V.rows() == collision_mesh_.num_vertices());

		const size_t num_vertices = collision_mesh_.num_vertices();

		if (constraint_set_.empty())
		{
			return Eigen::VectorXd::Zero(collision_mesh_.full_num_vertices());
		}

		const Eigen::MatrixXi &E = collision_mesh_.edges();
		const Eigen::MatrixXi &F = collision_mesh_.faces();

		auto storage = utils::create_thread_storage<Eigen::VectorXd>(Eigen::VectorXd::Zero(num_vertices));

		utils::maybe_parallel_for(constraint_set_.size(), [&](int start, int end, int thread_id) {
			Eigen::VectorXd &local_storage = utils::get_local_thread_storage(storage, thread_id);

			for (size_t i = start; i < end; i++)
			{
				// Quadrature weight is premultiplied by compute_potential
				const double potential = constraint_set_[i].compute_potential(V, E, F, dhat_);

				const int n_v = constraint_set_[i].num_vertices();
				const std::array<long, 4> vis = constraint_set_[i].vertex_ids(E, F);
				for (int j = 0; j < n_v; j++)
				{
					assert(0 <= vis[j] && vis[j] < num_vertices);
					local_storage[vis[j]] += potential / n_v;
				}
			}
		});

		Eigen::VectorXd out = Eigen::VectorXd::Zero(num_vertices);
		for (const auto &local_potential : storage)
		{
			out += local_potential;
		}

		Eigen::VectorXd out_full = Eigen::VectorXd::Zero(collision_mesh_.full_num_vertices());
		for (int i = 0; i < out.size(); i++)
			out_full[collision_mesh_.to_full_vertex_id(i)] = out[i];

		assert(std::abs(value_unweighted(x) - out_full.sum()) < 1e-10);

		return out_full;
	}

	void ContactForm::first_derivative_unweighted(const Eigen::VectorXd &x, Eigen::VectorXd &gradv) const
	{
		gradv = constraint_set_.compute_potential_gradient(collision_mesh_, compute_displaced_surface(x), dhat_);
		gradv = collision_mesh_.to_full_dof(gradv);
	}

	void ContactForm::second_derivative_unweighted(const Eigen::VectorXd &x, StiffnessMatrix &hessian) const
	{
		POLYFEM_SCOPED_TIMER("barrier hessian");
		hessian = constraint_set_.compute_potential_hessian(collision_mesh_, compute_displaced_surface(x), dhat_, project_to_psd_);
		hessian = collision_mesh_.to_full_dof(hessian);
	}

	void ContactForm::solution_changed(const Eigen::VectorXd &new_x)
	{
		update_constraint_set(compute_displaced_surface(new_x));
	}

	double ContactForm::max_step_size(const Eigen::VectorXd &x0, const Eigen::VectorXd &x1) const
	{
		// Extract surface only
		const Eigen::MatrixXd V0 = compute_displaced_surface(x0);
		const Eigen::MatrixXd V1 = compute_displaced_surface(x1);

		if (save_ccd_debug_meshes)
		{
			const Eigen::MatrixXi E = collision_mesh_.dim() == 2 ? Eigen::MatrixXi() : collision_mesh_.edges();
			const Eigen::MatrixXi &F = collision_mesh_.faces();
			igl::writePLY(resolve_output_path("debug_ccd_0.ply"), V0, F, E);
			igl::writePLY(resolve_output_path("debug_ccd_1.ply"), V1, F, E);
		}

		double max_step;
		if (use_cached_candidates_ && broad_phase_method_ != ipc::BroadPhaseMethod::SWEEP_AND_TINIEST_QUEUE_GPU)
			max_step = candidates_.compute_collision_free_stepsize(
				collision_mesh_, V0, V1, dmin_, ccd_tolerance_, ccd_max_iterations_);
		else
			max_step = ipc::compute_collision_free_stepsize(
				collision_mesh_, V0, V1, broad_phase_method_, ccd_tolerance_, ccd_max_iterations_);

#ifndef NDEBUG
		// This will check for static intersections as a failsafe. Not needed if we use our conservative CCD.
		Eigen::MatrixXd V_toi = (V1 - V0) * max_step + V0;

		while (ipc::has_intersections(collision_mesh_, V_toi))
		{
			logger().error("taking max_step results in intersections (max_step={:g})", max_step);
			max_step /= 2.0;

			const double Linf = (V_toi - V0).lpNorm<Eigen::Infinity>();
			if (max_step <= 0 || Linf == 0)
				log_and_throw_error("Unable to find an intersection free step size (max_step={:g} L∞={:g})", max_step, Linf);

			V_toi = (V1 - V0) * max_step + V0;
		}
#endif

		return max_step;
	}

	void ContactForm::line_search_begin(const Eigen::VectorXd &x0, const Eigen::VectorXd &x1)
	{
		candidates_.build(
			collision_mesh_,
			compute_displaced_surface(x0),
			compute_displaced_surface(x1),
			/*inflation_radius=*/dhat_ / 2,
			broad_phase_method_);

		use_cached_candidates_ = true;
	}

	void ContactForm::line_search_end()
	{
		candidates_.clear();
		use_cached_candidates_ = false;
	}

	void ContactForm::post_step(const int iter_num, const Eigen::VectorXd &x)
	{
		const Eigen::MatrixXd displaced_surface = compute_displaced_surface(x);

		const double curr_distance = constraint_set_.compute_minimum_distance(collision_mesh_, displaced_surface);

		if (use_adaptive_barrier_stiffness_)
		{
			if (is_time_dependent_)
			{
				const double prev_barrier_stiffness = barrier_stiffness();

				weight_ = ipc::update_barrier_stiffness(
					prev_distance_, curr_distance, max_barrier_stiffness_,
					barrier_stiffness(), ipc::world_bbox_diagonal_length(displaced_surface));

				if (barrier_stiffness() != prev_barrier_stiffness)
				{
					polyfem::logger().debug(
						"updated barrier stiffness from {:g} to {:g}",
						prev_barrier_stiffness, barrier_stiffness());
				}
			}
			else
			{
				// TODO: missing feature
				// update_barrier_stiffness(x);
			}
		}

		prev_distance_ = curr_distance;
	}

	bool ContactForm::is_step_collision_free(const Eigen::VectorXd &x0, const Eigen::VectorXd &x1) const
	{
		const auto displaced0 = compute_displaced_surface(x0);
		const auto displaced1 = compute_displaced_surface(x1);

		// Skip CCD if the displacement is zero.
		if ((displaced1 - displaced0).lpNorm<Eigen::Infinity>() == 0.0)
		{
			// Assumes initially intersection-free
			return true;
		}

		bool is_valid;
		if (use_cached_candidates_)
			is_valid = candidates_.is_step_collision_free(
				collision_mesh_, displaced0, displaced1, dmin_,
				ccd_tolerance_, ccd_max_iterations_);
		else
			is_valid = ipc::is_step_collision_free(
				collision_mesh_, displaced0, displaced1, broad_phase_method_,
				dmin_, ccd_tolerance_, ccd_max_iterations_);

		return is_valid;
	}
} // namespace polyfem::solver
