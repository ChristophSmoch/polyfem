#include <polyfem/AssemblerUtils.hpp>

namespace polyfem
{
	AssemblerUtils &AssemblerUtils::instance()
	{
		static AssemblerUtils instance;

		return instance;
	}


	AssemblerUtils::AssemblerUtils()
	{
		scalar_assemblers_.push_back("Laplacian");
		scalar_assemblers_.push_back("Helmholtz");

		tensor_assemblers_.push_back("LinearElasticity");
		tensor_assemblers_.push_back("HookeLinearElasticity");

		tensor_assemblers_.push_back("SaintVenant");
		tensor_assemblers_.push_back("NeoHookean");
		tensor_assemblers_.push_back("Ogden");

		stokes_assemblers_.push_back("Stokes");
	}

	bool AssemblerUtils::is_linear(const std::string &assembler) const
	{
		return assembler != "SaintVenant" && assembler != "NeoHookean" && assembler != "Ogden";
	}

	void AssemblerUtils::assemble_problem(const std::string &assembler,
		const bool is_volume,
		const int n_basis,
		const std::vector< ElementBases > &bases,
		const std::vector< ElementBases > &gbases,
		Eigen::SparseMatrix<double> &stiffness) const
	{
		if(assembler == "Helmholtz")
			helmholtz_.assemble(is_volume, n_basis, bases, gbases, stiffness);
		else if(assembler == "Laplacian")
			laplacian_.assemble(is_volume, n_basis, bases, gbases, stiffness);

		else if(assembler == "LinearElasticity")
			linear_elasticity_.assemble(is_volume, n_basis, bases, gbases, stiffness);
		else if(assembler == "HookeLinearElasticity")
			hooke_linear_elasticity_.assemble(is_volume, n_basis, bases, gbases, stiffness);
		else if(assembler == "Stokes")
			stokes_velocity_.assemble(is_volume, n_basis, bases, gbases, stiffness);

		else if(assembler == "SaintVenant")
			return;
		else if(assembler == "NeoHookean")
			return;
		else if(assembler == "Ogden")
			return;
		else
		{
			std::cerr<<"[Warning] "<<assembler<<" not found, fallback to default"<<std::endl;
			assert(false);
			laplacian_.assemble(is_volume, n_basis, bases, gbases, stiffness);
		}
	}

	void AssemblerUtils::assemble_mass_matrix(const std::string &assembler,
		const bool is_volume,
		const int n_basis,
		const std::vector< ElementBases > &bases,
		const std::vector< ElementBases > &gbases,
		Eigen::SparseMatrix<double> &mass) const
	{
		if(assembler == "Helmholtz" || assembler == "Laplacian")
			mass_mat_assembler_.assemble(is_volume, 1, n_basis, bases, gbases, mass);
		else
			mass_mat_assembler_.assemble(is_volume, is_volume? 3 : 2, n_basis, bases, gbases, mass);
	}

	void AssemblerUtils::assemble_mixed_problem(const std::string &assembler,
		const bool is_volume,
		const int n_psi_basis,
		const int n_phi_basis,
		const std::vector< ElementBases > &psi_bases,
		const std::vector< ElementBases > &phi_bases,
		const std::vector< ElementBases > &gbases,
		Eigen::SparseMatrix<double> &stiffness) const
	{
		if(assembler == "Stokes")
			stokes_pressure_.assemble(is_volume, n_psi_basis, n_phi_basis, psi_bases, phi_bases, gbases, stiffness);

		else
		{
			std::cerr<<"[Warning] "<<assembler<<" not found, fallback to default"<<std::endl;
			assert(false);
			stokes_pressure_.assemble(is_volume, n_psi_basis, n_phi_basis, psi_bases, phi_bases, gbases, stiffness);
		}
	}


	double AssemblerUtils::assemble_energy(const std::string &assembler,
		const bool is_volume,
		const std::vector< ElementBases > &bases,
		const std::vector< ElementBases > &gbases,
		const Eigen::MatrixXd &displacement) const
	{
		if(assembler == "SaintVenant")
			return saint_venant_elasticity_.compute_energy(is_volume, bases, gbases, displacement);
		else if(assembler == "NeoHookean")
			return neo_hookean_elasticity_.compute_energy(is_volume, bases, gbases, displacement);
		else if(assembler == "Ogden")
			return ogden_elasticity_.compute_energy(is_volume, bases, gbases, displacement);
		else
			return 0;
	}

	void AssemblerUtils::assemble_energy_gradient(const std::string &assembler,
		const bool is_volume,
		const int n_basis,
		const std::vector< ElementBases > &bases,
		const std::vector< ElementBases > &gbases,
		const Eigen::MatrixXd &displacement,
		Eigen::MatrixXd &grad) const
	{
		if(assembler == "SaintVenant")
			saint_venant_elasticity_.assemble(is_volume, n_basis, bases, gbases, displacement, grad);
		else if(assembler == "NeoHookean")
			neo_hookean_elasticity_.assemble(is_volume, n_basis, bases, gbases, displacement, grad);
		else if(assembler == "Ogden")
			ogden_elasticity_.assemble(is_volume, n_basis, bases, gbases, displacement, grad);
		else
			return;
	}

	void AssemblerUtils::assemble_energy_hessian(const std::string &assembler,
		const bool is_volume,
		const int n_basis,
		const std::vector< ElementBases > &bases,
		const std::vector< ElementBases > &gbases,
		const Eigen::MatrixXd &displacement,
		Eigen::SparseMatrix<double> &hessian) const
	{
		if(assembler == "SaintVenant")
			saint_venant_elasticity_.assemble_grad(is_volume, n_basis, bases, gbases, displacement, hessian);
		else if(assembler == "NeoHookean")
			neo_hookean_elasticity_.assemble_grad(is_volume, n_basis, bases, gbases, displacement, hessian);
		else if(assembler == "Ogden")
			ogden_elasticity_.assemble_grad(is_volume, n_basis, bases, gbases, displacement, hessian);
		else
			return;
	}


	void AssemblerUtils::compute_scalar_value(const std::string &assembler,
		const ElementBases &bs,
		const ElementBases &gbs,
		const Eigen::MatrixXd &local_pts,
		const Eigen::MatrixXd &fun,
		Eigen::MatrixXd &result) const
	{
		if(assembler == "Laplacian" || assembler == "Helmholtz" || assembler == "Stokes")
			return;

		else if(assembler == "LinearElasticity")
			linear_elasticity_.local_assembler().compute_von_mises_stresses(bs, gbs, local_pts, fun, result);
		else if(assembler == "HookeLinearElasticity")
			hooke_linear_elasticity_.local_assembler().compute_von_mises_stresses(bs, gbs, local_pts, fun, result);

		else if(assembler == "SaintVenant")
			saint_venant_elasticity_.local_assembler().compute_von_mises_stresses(bs, gbs, local_pts, fun, result);
		else if(assembler == "NeoHookean")
			neo_hookean_elasticity_.local_assembler().compute_von_mises_stresses(bs, gbs, local_pts, fun, result);
		else if(assembler == "Ogden")
			ogden_elasticity_.local_assembler().compute_von_mises_stresses(bs, gbs, local_pts, fun, result);

		else
		{
			std::cerr<<"[Warning] "<<assembler<<" not found, fallback to default"<<std::endl;
			assert(false);
			linear_elasticity_.local_assembler().compute_von_mises_stresses(bs, gbs, local_pts, fun, result);
		}
	}

	void AssemblerUtils::compute_tensor_value(const std::string &assembler,
		const ElementBases &bs,
		const ElementBases &gbs,
		const Eigen::MatrixXd &local_pts,
		const Eigen::MatrixXd &fun,
		Eigen::MatrixXd &result) const
	{
		if(assembler == "Laplacian" || assembler == "Helmholtz" || assembler == "Stokes")
			return;

		else if(assembler == "LinearElasticity")
			linear_elasticity_.local_assembler().compute_stress_tensor(bs, gbs, local_pts, fun, result);
		else if(assembler == "HookeLinearElasticity")
			hooke_linear_elasticity_.local_assembler().compute_stress_tensor(bs, gbs, local_pts, fun, result);

		else if(assembler == "SaintVenant")
			saint_venant_elasticity_.local_assembler().compute_stress_tensor(bs, gbs, local_pts, fun, result);
		else if(assembler == "NeoHookean")
			neo_hookean_elasticity_.local_assembler().compute_stress_tensor(bs, gbs, local_pts, fun, result);
		else if(assembler == "Ogden")
			ogden_elasticity_.local_assembler().compute_stress_tensor(bs, gbs, local_pts, fun, result);

		else
		{
			std::cerr<<"[Warning] "<<assembler<<" not found, fallback to default"<<std::endl;
			assert(false);
			linear_elasticity_.local_assembler().compute_stress_tensor(bs, gbs, local_pts, fun, result);
		}
	}


	VectorNd AssemblerUtils::compute_rhs(const std::string &assembler, const AutodiffHessianPt &pt) const
	{
		if(assembler == "Laplacian")
			return laplacian_.local_assembler().compute_rhs(pt);
		else if(assembler == "Helmholtz")
			return helmholtz_.local_assembler().compute_rhs(pt);

		else if(assembler == "LinearElasticity")
			return linear_elasticity_.local_assembler().compute_rhs(pt);
		else if(assembler == "HookeLinearElasticity")
			return hooke_linear_elasticity_.local_assembler().compute_rhs(pt);

		else if(assembler == "SaintVenant")
			return saint_venant_elasticity_.local_assembler().compute_rhs(pt);
		else if(assembler == "NeoHookean")
			return neo_hookean_elasticity_.local_assembler().compute_rhs(pt);
		else if(assembler == "Ogden")
			return ogden_elasticity_.local_assembler().compute_rhs(pt);

		else if(assembler == "Stokes")
			return stokes_velocity_.local_assembler().compute_rhs(pt);

		else
		{
			std::cerr<<"[Warning] "<<assembler<<" not found, fallback to default"<<std::endl;

			assert(false);
			return laplacian_.local_assembler().compute_rhs(pt);
		}

	}


	Eigen::Matrix<AutodiffScalarGrad, Eigen::Dynamic, 1, 0, 3, 1> AssemblerUtils::kernel(const std::string &assembler, const int dim, const AutodiffScalarGrad &r) const
	{
		if(assembler == "Laplacian")
			return laplacian_.local_assembler().kernel(dim, r);
		else if(assembler == "Helmholtz")
			return helmholtz_.local_assembler().kernel(dim, r);

		// else if(assembler == "LinearElasticity")
		// 	return linear_elasticity_.local_assembler().kernel(dim, r);
		// else if(assembler == "HookeLinearElasticity")
		// 	return hooke_linear_elasticity_.local_assembler().kernel(dim, r);

		// else if(assembler == "SaintVenant")
		// 	return saint_venant_elasticity_.local_assembler().kernel(dim, r);
		// else if(assembler == "NeoHookean")
		// 	return neo_hookean_elasticity_.local_assembler().kernel(dim, r);
		// else if(assembler == "Ogden")
		// 	return ogden_elasticity_.local_assembler().kernel(dim, r);

		else
		{
			std::cerr<<"[Warning] "<<assembler<<" not found, fallback to default"<<std::endl;

			assert(false);
			return laplacian_.local_assembler().kernel(dim, r);
		}
	}


	void AssemblerUtils::clear_cache()
	{
		saint_venant_elasticity_.clear_cache();
		neo_hookean_elasticity_.clear_cache();
		ogden_elasticity_.clear_cache();
	}



	void AssemblerUtils::set_parameters(const json &params)
	{
		laplacian_.local_assembler().set_parameters(params);
		helmholtz_.local_assembler().set_parameters(params);

		linear_elasticity_.local_assembler().set_parameters(params);
		hooke_linear_elasticity_.local_assembler().set_parameters(params);

		saint_venant_elasticity_.local_assembler().set_parameters(params);
		neo_hookean_elasticity_.local_assembler().set_parameters(params);
		ogden_elasticity_.local_assembler().set_parameters(params);

		stokes_velocity_.local_assembler().set_parameters(params);
	}

}
