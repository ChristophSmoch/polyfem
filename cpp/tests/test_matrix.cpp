////////////////////////////////////////////////////////////////////////////////
#include "MatrixUtils.hpp"
#include "auto_eigs.hpp"
#include "AutodiffTypes.hpp"

#include <iostream>

#include <Eigen/Dense>

#include <catch.hpp>
////////////////////////////////////////////////////////////////////////////////

using namespace poly_fem;

TEST_CASE("determinant2", "[matrix]") {
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, 3, 3> mat(2,2);
    mat.setRandom();

    REQUIRE(poly_fem::determinant(mat) == Approx(mat.determinant()).margin(1e-12));
}

TEST_CASE("determinant3", "[matrix]") {
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, 0, 3, 3> mat(3,3);
    mat.setRandom();

    REQUIRE(poly_fem::determinant(mat) == Approx(mat.determinant()).margin(1e-12));
}


TEST_CASE("eigs2", "[matrix]") {
    Eigen::Matrix<double, 2, 2> mat;
    mat <<
    	0.679900336682452,   0.853956838129916,
   		0.853956838129916,   1.654714487848889;

   	Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 3, 1> actual_eigs(2);
   	autogen::eigs_2d<double>(mat, actual_eigs);

   	const auto e0 = Approx(0.184043491073894).margin(1e-8);
   	const auto e1 = Approx(2.150571333457448).margin(1e-8);

    REQUIRE(actual_eigs(0) == e0);
    REQUIRE(actual_eigs(1) == e1);
}


TEST_CASE("eigs2autodiff", "[matrix]") {
	typedef DScalar2<double, Eigen::Matrix<double, 4, 1>, Eigen::Matrix<double, 4, 4>> T;
	typedef Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, 0, 3, 3> 	AutoDiffGradMat;

    Eigen::Matrix<double, 2, 2> tmp;
    tmp <<
    	0.679900336682452,   0.853956838129916,
   		0.853956838129916,   1.654714487848889;

	DiffScalarBase::setVariableCount(4);
   	AutoDiffGradMat mat(2,2);
   	for(int i = 0; i < 4; ++i)
   		mat(i) = T(i, tmp(i));


   	Eigen::Matrix<T, Eigen::Dynamic, 1, 0, 3, 1> actual_eigs(2);
   	autogen::eigs_2d<T>(mat, actual_eigs);

   	const auto e0 = Approx(0.184043491073894).margin(1e-8);
   	const auto e1 = Approx(2.150571333457448).margin(1e-8);

    REQUIRE(actual_eigs(0).getValue() == e0);
    REQUIRE(actual_eigs(1).getValue() == e1);
}


TEST_CASE("eigs3", "[matrix]") {
    Eigen::Matrix<double, 3, 3> mat;
    mat <<
    	0.723799495850613,   0.738301489180688,   0.854652664143603,
   		0.738301489180688,   1.868383818516662,   1.880071527849840,
   		0.854652664143603,   1.880071527849840,   1.924739710079838;

   	Eigen::Matrix<double, Eigen::Dynamic, 1, 0, 3, 1> actual_eigs(3);
   	autogen::eigs_3d<double>(mat, actual_eigs);


   	const auto e0 = Approx(0.002128421644668).margin(1e-8);
   	const auto e1 = Approx(0.366887383112958).margin(1e-8);
   	const auto e2 = Approx(4.147907219689489).margin(1e-8);

    REQUIRE(actual_eigs(0) == e0);
    REQUIRE(actual_eigs(1) == e1);
    REQUIRE(actual_eigs(2) == e2);
}


TEST_CASE("eigs3autodiff", "[matrix]") {
	// typedef DScalar2<double, Eigen::Matrix<double, 9, 1>, Eigen::Matrix<double, 9, 9>> T;
 //    typedef Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic, 0, 3, 3> 	AutoDiffGradMat;
 //    Eigen::Matrix<double, 3, 3> tmp;
 //    tmp <<
 //    	0.723799495850613,   0.738301489180688,   0.854652664143603,
 //   		0.738301489180688,   1.868383818516662,   1.880071527849840,
 //   		0.854652664143603,   1.880071527849840,   1.924739710079838;

		// DiffScalarBase::setVariableCount(9);
 //   	AutoDiffGradMat mat(3,3);
 //   	for(int i = 0; i < 9; ++i)
 //   		mat(i) = T(i, tmp(i));

 //   	Eigen::Matrix<T, Eigen::Dynamic, 1, 0, 3, 1> actual_eigs(3);
 //   	autogen::eigs_3d<T>(mat, actual_eigs);

 //   	const auto e0 = Approx(0.002128421644668).margin(1e-8);
 //   	const auto e1 = Approx(0.366887383112958).margin(1e-8);
 //   	const auto e2 = Approx(4.147907219689489).margin(1e-8);

 //    REQUIRE(actual_eigs(0).getValue() == e0);
 //    REQUIRE(actual_eigs(1).getValue() == e1);
 //    REQUIRE(actual_eigs(2).getValue() == e2);
}
