#pragma once

#include "AssemblerData.hpp"
#include "MatParams.hpp"

#include <polyfem/Common.hpp>

#include <polyfem/utils/ElasticityUtils.hpp>

#include <polyfem/utils/AutodiffTypes.hpp>

#include <Eigen/Dense>
#include <functional>

// local assembler for linear elasticity
namespace polyfem::assembler
{
	class LinearElasticity
	{
	public:
		/// computes local stiffness matrix is R^{dim²} for bases i,j
		// vals stores the evaluation for that element
		// da contains both the quadrature weight and the change of metric in the integral
		Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 9, 1>
		assemble(const LinearAssemblerData &data) const;

		// neccessary for mixing linear model with non-linear collision response
		Eigen::MatrixXd assemble_hessian(const NonLinearAssemblerData &data) const;
		// compute gradient of elastic energy, as assembler
		Eigen::VectorXd assemble_grad(const NonLinearAssemblerData &data) const;
		// compute elastic energy
		double compute_energy(const NonLinearAssemblerData &data) const;

		// kernel of the pde, used in kernel problem
		Eigen::Matrix<AutodiffScalarGrad, Eigen::Dynamic, 1, 0, 3, 1> kernel(const int dim, const AutodiffGradPt &r) const;

		// uses autodiff to compute the rhs for a fabbricated solution
		// uses autogenerated code to compute div(sigma)
		// pt is the evaluation of the solution at a point
		Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 3, 1>
		compute_rhs(const AutodiffHessianPt &pt) const;

		// compute von mises stress for an element at the local points
		void compute_von_mises_stresses(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, Eigen::MatrixXd &stresses) const;
		// compute stress tensor for an element at the local points
		void compute_stress_tensor(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, const ElasticityTensorType &type, Eigen::MatrixXd &tensor) const;

		void compute_dstress_dgradu_multiply_mat(const int el_id, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &global_pts, const Eigen::MatrixXd &grad_u_i, const Eigen::MatrixXd &mat, Eigen::MatrixXd &stress, Eigen::MatrixXd &result) const;
		void compute_dstress_dmu_dlambda(const int el_id, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &global_pts, const Eigen::MatrixXd &grad_u_i, Eigen::MatrixXd &dstress_dmu, Eigen::MatrixXd &dstress_dlambda) const;

		// size of the problem, this is a tensor problem so the size is the size of the mesh
		void set_size(const int size) { size_ = size; }
		inline int size() const { return size_; }

		// inialize material parameter
		void add_multimaterial(const int index, const json &params);

		// class that stores and compute lame parameters per point
		const LameParameters &lame_params() const { return params_; }
		void set_params(const LameParameters &params) { params_ = params; }

	private:
		int size_ = -1;
		// class that stores and compute lame parameters per point
		LameParameters params_;

		void assign_stress_tensor(const int el_id, const basis::ElementBases &bs, const basis::ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, const int all_size, const ElasticityTensorType &type, Eigen::MatrixXd &all, const std::function<Eigen::MatrixXd(const Eigen::MatrixXd &)> &fun) const;

		// aux function that computes energy
		// double compute_energy is the same with T=double
		// assemble_grad is the same with T=DScalar1 and return .getGradient()
		template <typename T>
		T compute_energy_aux(const NonLinearAssemblerData &data) const;
	};
} // namespace polyfem::assembler
