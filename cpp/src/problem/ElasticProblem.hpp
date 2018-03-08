#ifndef ELASTIC_PROBLEM_HPP
#define ELASTIC_PROBLEM_HPP

#include "Problem.hpp"

#include <vector>
#include <Eigen/Dense>

namespace poly_fem
{
	class ElasticProblem: public Problem
	{
	public:
		ElasticProblem(const std::string &name);

		void rhs(const Mesh &mesh, const Eigen::MatrixXd &pts, Eigen::MatrixXd &val) const override;
		void bc(const Mesh &mesh, const Eigen::MatrixXi &global_ids, const Eigen::MatrixXd &pts, Eigen::MatrixXd &val) const override;

		bool has_exact_sol() const override { return false; }
		bool is_scalar() const override { return false; }
	};


	class ElasticProblemExact: public Problem
	{
	public:
		ElasticProblemExact(const std::string &name);

		void rhs(const Mesh &mesh, const Eigen::MatrixXd &pts, Eigen::MatrixXd &val) const override;
		void bc(const Mesh &mesh, const Eigen::MatrixXi &global_ids, const Eigen::MatrixXd &pts, Eigen::MatrixXd &val) const override;

		void exact(const Eigen::MatrixXd &pts, Eigen::MatrixXd &val) const override;

		bool has_exact_sol() const override { return true; }
		bool is_scalar() const override { return false; }
	};
}

#endif //ELASTIC_PROBLEM_HPP
