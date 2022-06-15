#include "ImplicitNewmark.hpp"

namespace polyfem
{
	namespace time_integrator
	{
		void ImplicitNewmark::set_parameters(const nlohmann::json &params)
		{
			gamma = params.value("gamma", 0.5);
			beta = params.value("beta", 0.25);
		}

		void ImplicitNewmark::update_quantities(const Eigen::VectorXd &x)
		{
			Eigen::VectorXd tmp = x_prev() + dt() * (v_prev() + dt() * (0.5 - beta) * a_prev());
			v_prev() += dt() * (1 - gamma) * a_prev();   // vᵗ + h(1 - γ)aᵗ
			a_prev() = (x - tmp) / (beta * dt() * dt()); // aᵗ⁺¹ = ...
			v_prev() += dt() * gamma * a_prev();         // hγaᵗ⁺¹
			x_prev() = x;
		}

		Eigen::VectorXd ImplicitNewmark::x_tilde() const
		{
			return x_prev() + dt() * (v_prev() + dt() * (0.5 - beta) * a_prev());
		}

		double ImplicitNewmark::acceleration_scaling() const
		{
			return beta * dt() * dt();
		}
	} // namespace time_integrator
} // namespace polyfem
