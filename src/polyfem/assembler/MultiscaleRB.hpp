#pragma once

#include "AssemblerData.hpp"

#include <polyfem/Common.hpp>
#include <polyfem/utils/ElasticityUtils.hpp>

#include <polyfem/basis/ElementBases.hpp>

#include <polyfem/utils/AutodiffTypes.hpp>
#include <polyfem/utils/Types.hpp>

#include <Eigen/Dense>
#include <array>

// non linear NeoHookean material model
namespace polyfem::assembler
{
	class MultiscaleRB
	{
	public:
		MultiscaleRB();
		~MultiscaleRB();

		// energy, gradient, and hessian used in newton method
		Eigen::MatrixXd assemble_hessian(const NonLinearAssemblerData &data) const;
		Eigen::VectorXd assemble_grad(const NonLinearAssemblerData &data) const;

		double compute_energy(const NonLinearAssemblerData &data) const;

		// rhs for fabbricated solution, compute with automatic sympy code
		Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 3, 1>
		compute_rhs(const AutodiffHessianPt &pt) const;

		inline int size() const { return size_; }
		void set_size(const int size);

		// von mises and stress tensor
		void compute_von_mises_stresses(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, Eigen::MatrixXd &stresses) const;
		void compute_stress_tensor(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, Eigen::MatrixXd &tensor) const;

		// sets material params
		void add_multimaterial(const int index, const json &params);
		void set_params(const LameParameters &params) { params_ = params; }
		LameParameters &lame_params() { return params_; }
		const LameParameters &lame_params() const { return params_; }

	private:
		int size_ = -1;

		LameParameters params_;

		// utulity function that computes energy, the template is used for double, DScalar1, and DScalar2 in energy, gradient and hessian
		double compute_energy_aux(const NonLinearAssemblerData &data) const;
		template <int n_basis, int dim>
		void compute_energy_hessian_aux_fast(const NonLinearAssemblerData &data, Eigen::MatrixXd &H) const;
		template <int n_basis, int dim>
		void compute_energy_aux_gradient_fast(const NonLinearAssemblerData &data, Eigen::VectorXd &G_flattened) const;

		void assign_stress_tensor(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, const int all_size, Eigen::MatrixXd &all, const std::function<Eigen::MatrixXd(const Eigen::MatrixXd &)> &fun) const;

		// microstructure
		json unit_cell_args;
		Eigen::MatrixXd reduced_basis; // (n_bases*dim) * N
		void sample_def_grads(std::vector<Eigen::MatrixXd> &def_grads) const;
		void create_reduced_basis(const std::vector<Eigen::MatrixXd> &def_grads);
		void projection(const Eigen::MatrixXd &F, Eigen::VectorXd &xi) const;
		void homogenization(const Eigen::MatrixXd &def_grad, double &energy, Eigen::MatrixXd &stress, Eigen::MatrixXd &stiffness) const;

		Eigen::VectorXd sample_det;
		Eigen::VectorXd sample_amp;
		int n_sample_dir = 5;
	};
} // namespace polyfem::assembler
