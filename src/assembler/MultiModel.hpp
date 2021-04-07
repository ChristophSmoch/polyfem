#pragma once

#include <polyfem/Common.hpp>
#include <polyfem/ElasticityUtils.hpp>

#include <polyfem/LinearElasticity.hpp>
// #include <polyfem/SaintVenantElasticity.hpp>
#include <polyfem/NeoHookeanElasticity.hpp>

#include <polyfem/ElementAssemblyValues.hpp>
#include <polyfem/ElementBases.hpp>
#include <polyfem/AutodiffTypes.hpp>
#include <polyfem/Types.hpp>

#include <Eigen/Dense>
#include <array>

namespace polyfem
{
	class MultiModel
	{
	public:
		//neccessary for mixing linear model with non-linear collision response
		Eigen::MatrixXd assemble_hessian(const ElementAssemblyValues &vals, const Eigen::MatrixXd &displacement, const QuadratureVector &da) const;
		//compute gradient of elastic energy, as assembler
		Eigen::VectorXd assemble_grad(const ElementAssemblyValues &vals, const Eigen::MatrixXd &displacement, const QuadratureVector &da) const;
		//compute elastic energy
		double compute_energy(const ElementAssemblyValues &vals, const Eigen::MatrixXd &displacement, const QuadratureVector &da) const;

		//uses autodiff to compute the rhs for a fabbricated solution
		//uses autogenerated code to compute div(sigma)
		//pt is the evaluation of the solution at a point
		Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 3, 1>
		compute_rhs(const AutodiffHessianPt &pt) const;

		//compute von mises stress for an element at the local points
		void compute_von_mises_stresses(const int el_id, const ElementBases &bs, const ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, Eigen::MatrixXd &stresses) const;
		//compute stress tensor for an element at the local points
		void compute_stress_tensor(const int el_id, const ElementBases &bs, const ElementBases &gbs, const Eigen::MatrixXd &local_pts, const Eigen::MatrixXd &displacement, Eigen::MatrixXd &tensor) const;

		//size of the problem, this is a tensor problem so the size is the size of the mesh
		inline int size() const { return size_; }
		void set_size(const int size);

		//inialize material parameter
		void set_parameters(const json &params);
		//initialize material param per element
		void init_multimaterial(const Eigen::MatrixXd &Es, const Eigen::MatrixXd &nus);
		//initialized multi models
		inline void init_multimodels(const std::vector<std::string> &mats) { multi_material_models_ = mats; }

		//class that stores and compute lame parameters per point
		const LameParameters &lame_params() const { return linear_elasticity_.lame_params(); }

	private:
		int size_ = 2;
		std::vector<std::string> multi_material_models_;

		// SaintVenantElasticity saint_venant_;
		NeoHookeanElasticity neo_hookean_;
		LinearElasticity linear_elasticity_;
	};
} // namespace polyfem
