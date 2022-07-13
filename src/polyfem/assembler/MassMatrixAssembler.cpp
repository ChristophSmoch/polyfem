#include "MassMatrixAssembler.hpp"

#include <polyfem/utils/MaybeParallelFor.hpp>
#include <polyfem/utils/Logger.hpp>

namespace polyfem::assembler
{
	using namespace polyfem::basis;
	using namespace polyfem::quadrature;
	using namespace polyfem::utils;

	namespace
	{
		class LocalThreadMatStorage
		{
		public:
			std::vector<Eigen::Triplet<double>> entries;
			StiffnessMatrix tmp_mat;
			StiffnessMatrix mass_mat;
			ElementAssemblyValues vals;
			QuadratureVector da;

			LocalThreadMatStorage()
			{
			}

			LocalThreadMatStorage(const int buffer_size, const int mat_size)
			{
				init(buffer_size, mat_size);
			}

			void init(const int buffer_size, const int mat_size)
			{
				entries.reserve(buffer_size);
				tmp_mat.resize(mat_size, mat_size);
				mass_mat.resize(mat_size, mat_size);
			}

			void condense()
			{
				if (entries.size() >= 1e8)
				{
					tmp_mat.setFromTriplets(entries.begin(), entries.end());
					mass_mat += tmp_mat;
					mass_mat.makeCompressed();

					tmp_mat.setZero();
					tmp_mat.data().squeeze();

					mass_mat.makeCompressed();

					entries.clear();
					logger().debug("cleaning memory...");
				}
			}
		};
	} // namespace

	void MassMatrixAssembler::assemble(
		const bool is_volume,
		const int size,
		const int n_basis,
		const Density &density,
		const std::vector<ElementBases> &bases,
		const std::vector<ElementBases> &gbases,
		const AssemblyValsCache &cache,
		StiffnessMatrix &mass) const
	{
		const int buffer_size = std::min(long(1e8), long(n_basis) * size);
		logger().debug("buffer_size {}", buffer_size);

		mass.resize(n_basis * size, n_basis * size);
		mass.setZero();

		auto storage = create_thread_storage(LocalThreadMatStorage(buffer_size, mass.rows()));

		const int n_bases = int(bases.size());

		maybe_parallel_for(n_bases, [&](int start, int end, int thread_id) {
			LocalThreadMatStorage &local_storage = get_local_thread_storage(storage, thread_id);

			for (int e = start; e < end; ++e)
			{
				ElementAssemblyValues &vals = local_storage.vals;
				// vals.compute(e, is_volume, bases[e], gbases[e]);
				cache.compute(e, is_volume, bases[e], gbases[e], vals);

				const Quadrature &quadrature = vals.quadrature;

				assert(MAX_QUAD_POINTS == -1 || quadrature.weights.size() < MAX_QUAD_POINTS);
				local_storage.da = vals.det.array() * quadrature.weights.array();
				const int n_loc_bases = int(vals.basis_values.size());

				for (int i = 0; i < n_loc_bases; ++i)
				{
					const auto &global_i = vals.basis_values[i].global;

					for (int j = 0; j <= i; ++j)
					{
						const auto &global_j = vals.basis_values[j].global;

						double tmp = 0; //(vals.basis_values[i].val.array() * vals.basis_values[j].val.array() * da.array()).sum();
						for (int q = 0; q < local_storage.da.size(); ++q)
						{
							const double rho = density(vals.quadrature.points.row(q), vals.val.row(q), vals.element_id);
							tmp += rho * vals.basis_values[i].val(q) * vals.basis_values[j].val(q) * local_storage.da(q);
						}
						if (std::abs(tmp) < 1e-30)
						{
							continue;
						}

						for (int n = 0; n < size; ++n)
						{
							//local matrix is diagonal
							const int m = n;
							// for(int m = 0; m < size; ++m)
							{
								const double local_value = tmp; //val(n*size+m);
								for (size_t ii = 0; ii < global_i.size(); ++ii)
								{
									const auto gi = global_i[ii].index * size + m;
									const auto wi = global_i[ii].val;

									for (size_t jj = 0; jj < global_j.size(); ++jj)
									{
										const auto gj = global_j[jj].index * size + n;
										const auto wj = global_j[jj].val;

										local_storage.entries.emplace_back(gi, gj, local_value * wi * wj);
										if (j < i)
										{
											local_storage.entries.emplace_back(gj, gi, local_value * wj * wi);
										}

										local_storage.condense();
									}
								}
							}
						}

						// t1.stop();
						// if (!vals.has_parameterization) { std::cout << "-- t1: " << t1.getElapsedTime() << std::endl; }
					}
				}

				// timer.stop();
				// if (!vals.has_parameterization) { std::cout << "-- Timer: " << timer.getElapsedTime() << std::endl; }
			}
		});

		// Serially merge local storages
		for (LocalThreadMatStorage &local_storage : storage)
		{
			mass += local_storage.mass_mat;
			local_storage.tmp_mat.setFromTriplets(local_storage.entries.begin(), local_storage.entries.end());
			mass += local_storage.tmp_mat;
		}
		mass.makeCompressed();
	}

	/*
	void MassMatrixAssembler::assemble_cross(
		const bool is_volume,
		const int size,
		const int n_basis_a,
		const std::vector<ElementBases> &bases_a,
		const std::vector<ElementBases> &gbases_a,
		const int n_basis_b,
		const std::vector<ElementBases> &bases_b,
		const std::vector<ElementBases> &gbases_b,
		const AssemblyValsCache &cache,
		StiffnessMatrix &mass) const
	{
		const int buffer_size = std::min(long(1e8), long(std::max(n_basis_a, n_basis_b)) * size);
		logger().debug("buffer_size {}", buffer_size);

		mass.resize(n_basis_b * size, n_basis_a * size);
		mass.setZero();

		auto storage = create_thread_storage(LocalThreadMatStorage(buffer_size, mass.rows()));

		const int n_bases_a = int(bases_a.size());
		const int n_bases_b = int(bases_b.size());

		// TODO: Use a AABB tree to find all intersecting elements then loop over only those pairs

		// maybe_parallel_for(n_bases_b, [&](int start, int end, int thread_id) {
		// LocalThreadMatStorage &local_storage = get_local_thread_storage(storage, thread_id);

		std::vector<Eigen::Triplet<double>> triplets;

		for (int eb = 0; eb < n_bases_b; ++eb)
		{
			for (int ea = 0; ea < n_bases_a; ++ea)
			{
				ElementAssemblyValues &vals = local_storage.vals;
				// vals.compute(e, is_volume, bases[e], gbases[e]);
				// TODO:
				cache.compute(e, is_volume, bases[e], gbases[e], vals);

				const Quadrature &quadrature = vals.quadrature;

				assert(MAX_QUAD_POINTS == -1 || quadrature.weights.size() < MAX_QUAD_POINTS);
				local_storage.da = vals.det.array() * quadrature.weights.array();

				const int n_loc_bases = int(vals.basis_values.size());
				for (int i = 0; i < n_loc_bases; ++i)
				{
					const auto &global_i = vals.basis_values[i].global;

					for (int j = 0; j <= i; ++j)
					{
						const auto &global_j = vals.basis_values[j].global;

						double tmp = 0; //(vals.basis_values[i].val.array() * vals.basis_values[j].val.array() * da.array()).sum();
						for (int q = 0; q < local_storage.da.size(); ++q)
						{
							// const double rho = density(vals.quadrature.points.row(q), vals.val.row(q), vals.element_id);
							const double rho = 1;
							tmp += rho * vals.basis_values[i].val(q) * vals.basis_values[j].val(q) * local_storage.da(q);
						}
						if (std::abs(tmp) < 1e-30)
						{
							continue;
						}

						for (int n = 0; n < size; ++n)
						{
							//local matrix is diagonal
							const int m = n;
							// for(int m = 0; m < size; ++m)
							{
								const double local_value = tmp; //val(n*size+m);
								for (size_t ii = 0; ii < global_i.size(); ++ii)
								{
									const auto gi = global_i[ii].index * size + m;
									const auto wi = global_i[ii].val;

									for (size_t jj = 0; jj < global_j.size(); ++jj)
									{
										const auto gj = global_j[jj].index * size + n;
										const auto wj = global_j[jj].val;

										local_storage.entries.emplace_back(gi, gj, local_value * wi * wj);
										if (j < i)
										{
											local_storage.entries.emplace_back(gj, gi, local_value * wj * wi);
										}

										local_storage.condense();
									}
								}
							}
						}
					}
				}
			}
		}
		// });

		// Serially merge local storages
		for (LocalThreadMatStorage &local_storage : storage)
		{
			mass += local_storage.mass_mat;
			local_storage.tmp_mat.setFromTriplets(local_storage.entries.begin(), local_storage.entries.end());
			mass += local_storage.tmp_mat;
		}
		mass.makeCompressed();
	}
	*/

} // namespace polyfem::assembler
