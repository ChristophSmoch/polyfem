#pragma once

#include <polyfem/utils/Types.hpp>

#include <Eigen/Core>

namespace polyfem::mesh
{
	void stitch_mesh(
		const Eigen::MatrixXd &V,
		const Eigen::MatrixXi &F,
		Eigen::MatrixXd &V_out,
		Eigen::MatrixXi &F_out,
		const double epsilon = 1e-5);

	double max_edge_length(const Eigen::MatrixXd &V, const Eigen::MatrixXi &F);

	// Regular tessilation

	void regular_grid_triangle_barycentric_coordinates(
		const int n, Eigen::MatrixXd &V, Eigen::MatrixXi &F);

	Eigen::MatrixXd sample_triangle(
		const VectorNd &a,
		const VectorNd &b,
		const VectorNd &c,
		const Eigen::MatrixXd &coords);

	void regular_grid_tessilation(
		const Eigen::MatrixXd &V,
		const Eigen::MatrixXi &F,
		const double max_edge_length,
		Eigen::MatrixXd &V_out,
		Eigen::MatrixXi &F_out);

	// Irregular tessilation

	Eigen::MatrixXd
	refine_edge(const VectorNd &a, const VectorNd &b, const double max_edge_length);

	void refine_triangle_edges(
		const VectorNd &a,
		const VectorNd &b,
		const VectorNd &c,
		const double max_edge_len,
		Eigen::MatrixXd &V,
		Eigen::MatrixXi &E);

#ifdef POLYFEM_WITH_TRIANGLE
	void irregular_triangle(
		const Eigen::Vector3d &a,
		const Eigen::Vector3d &b,
		const Eigen::Vector3d &c,
		const double max_edge_length,
		Eigen::MatrixXd &V,
		Eigen::MatrixXi &F);

	void irregular_tessilation(
		const Eigen::MatrixXd &V,
		const Eigen::MatrixXi &F,
		const double max_edge_length,
		Eigen::MatrixXd &V_out,
		Eigen::MatrixXi &F_out);
#endif
} // namespace polyfem::mesh