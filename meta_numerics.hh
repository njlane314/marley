// This file contains a C++ translation of code for computing the Coulomb wavefunctions
// (see, e.g, http://dlmf.nist.gov/33.2 and
// http://mathworld.wolfram.com/CoulombWaveFunction.html) excerpted from the Meta Numerics
// library (http://metanumerics.codeplex.com/), which was originally written in C#. The
// contents of this file and its accompanying source file (meta_numerics.cc), but *not*
// the rest of MARLEY, are distributed under the terms of the Microsoft Public License
// (Ms-PL). The full text of the license is reproduced at the end of this file, and it may
// also be viewed at https://metanumerics.codeplex.com/license.
//
// Original Meta Numerics Library Copyright (c) 2008-2015 David Wright
// C++ Translation and Adaptation for MARLEY Copyright (c) 2015 Steven Gardiner

#include <cmath>
#include <complex>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "marley_utils.hh"

// -- Begin Ms-PL licensed code
namespace meta_numerics {

  // Sign function taken from http://stackoverflow.com/a/4609795/4081973
  template <typename T> inline int sign(T val) {
    return (T(0) < val) - (val < T(0));
  }

  // the even Bernoulli numbers B_2n = Bernoulli[n]
  // the only nonvanishing odd Bernoulli number is B_1 = -1/2, which must be handled seperately if you
  // use these numbers in any series expansion
  const std::vector<double> Bernoulli = { 1.0, 1.0 / 6.0,
    -1.0 / 30.0, 1.0 / 42.0, -1.0 / 30.0, 5.0 / 66.0, -691.0 / 2730.0, 7.0 / 6.0,
    -3617.0 / 510.0, 43867.0 / 798.0, -174611.0 / 330.0, 854513.0 / 138.0,
    -236364091.0 / 2730.0, 8553103.0 / 6.0, -23749461029.0 / 870.0,
    8615841276005.0 / 14322.0
  };

  // maximum number of iterations of a series
  constexpr int SeriesMax = 250;

  // double dedicates 52 bits to the magnitude of the mantissa, so 2^-52 is the smallest fraction difference
  // it can detect; in order to avoid any funny effects at the margin, we try for one byte less, 2^-49
  constexpr double MaxAccuracy = std::pow(2.0, -49);

  /// <summary>
  /// The Euler constant.
  /// </summary>
  /// <remarks><para>The Euler constant &#x3B3; = 0.5772...</para></remarks>
  /// <seealso href="http://en.wikipedia.org/wiki/Euler_gamma"/>
  /// <seealso href="http://mathworld.wolfram.com/Euler-MascheroniConstant.html" />
  constexpr double EulerGamma = 0.577215664901532860606512;

  // the following infrastructure is for numerical integration of ODEs
  // eventually we should expose it, but for now it is just for computing Coulomb wave functions

  class OdeStepper {

    public:
      /// <summary>
      /// The current value of the independent variable.
      /// </summary>
      double X;

      /// <summary>
      /// The current value of the dependent variable.
      /// </summary>
      double Y;

      /// <summary>
      /// The current step size.
      /// </summary>
      double DeltaX;

      inline int EvaluationCount() {
        return count;
      }

      /// <summary>
      /// The target accuracy.
      /// </summary>
      double accuracy;

      inline void set_accuracy(double value) {
        if ((value < MaxAccuracy) || (value >= 1.0))
        throw std::runtime_error(std::string("Invalid accuracy value ") + std::to_string(value)
          + " encountered in OdeStepper::Accuracy");
        accuracy = value;
      }

      /// <summary>
      /// The right-hand side of the differential equation.
      /// </summary>
      std::function<double(double, double)> RightHandSide;

      virtual void Step () = 0;

      void Integrate (double X1);

    protected:

      int count;

      inline double Evaluate (double x, double y) {
        count++;
        return (RightHandSide(x, y));
      }
  };

  class BulrischStoerStoermerStepper : public OdeStepper {

    public:

      double YPrime;

      void Step();

    private:

      static const std::vector<int> N;

      int target_k = 0;

      // do a step consisting of n mini-steps
      void TrialStep (int n, double& Y1, double& Y1P);
  };

  double Reduce (double x, double y);

  // This class handles the Lanczos approximation to the \Gamma function and the
  // correspoding approximations to associated functions.  For basic background to the
  // Lanczos approximation, see http://en.wikipedia.org/wiki/Lanczos_approximation and
  // http://mathworld.wolfram.com/LanczosApproximation.html and
  // http://www.boost.org/doc/libs/1_53_0/libs/math/doc/sf_and_dist/html/math_toolkit/backgrounders/lanczos.html.
  // The basic Lanczos formula is: \Gamma(z+1) = \sqrt{2 \pi} (z + g + 1/2)^(z+1/2) e^{-(z +
  // g + 1/2)} \left[ c_0 + \frac{c_1}{z+1} + \frac{c_2}{z+2} + \cdots + \frac{c_N}{z+N}
  // \right] Given a value of g, the c-values can be computed using a complicated set of
  // matrix equations that require high precision.  We write this as: \Gamma(z) = \sqrt{2
  // \pi} (z + g - 1/2)^(z-1/2) e^{-(z + g - 1/2)} \left[ c_0 + \frac{c_1}{z} +
  // \frac{c_2}{z+1} + \cdots + \frac{c_N}{z+N-1} \right] = \sqrt{2 \pi} (\frac{z + g -
  // 1/2}{e})^{z-1/2} e^{-g} \left[ c_0 + \frac{c_1}{z} + \frac{c_2}{z+1} + \cdots +
  // \frac{c_N}{z+N-1} \right]

  class Lanczos {

    // These are listed at http://www.mrob.com/pub/ries/lanczos-gamma.html as the values
    // used by GSL, although I don't know if they still are.  Measured deviations at
    // integers 2, 2, 4, 11, 1, 17, 22, 21 X 10^(-16) so this is clearly worse than
    // Godfrey's coefficients, although it does manage with slightly fewer terms.
    /*
    private const double LanczosG = 7.0; private static readonly double[] LanczosC =
      double[] { 0.99999999999980993, 676.5203681218851, -1259.1392167224028,
      771.32342877765313, -176.61502916214059, 12.507343278686905, -0.13857109526572012,
      9.9843695780195716e-6, 1.5056327351493116e-7 };
    */

    // Godfrey's coefficients, claimed relative error < 10^(-15), documented at
    // http://my.fit.edu/~gabdo/gamma.txt and in NR 3rd edition section 6.1.  Measured
    // relative deviation at integers 1, 1, 4, 1, 4, 5, 6, 3 X 10^(-16) so this appears
    // about right.  These improves to 1, 1, 2, 1, 3, 3, 3 X 10^(-16) when we pull the 1/e
    // into Math.Pow(t/e, z+1/2) instead of calling Math.Exp(-t) seperately.

    private:

      static constexpr double LanczosG = 607.0 / 128.0;

      static const std::vector<double> LanczosC;

      // These coefficients are given by Pugh in his thesis
      // (http://web.viu.ca/pughg/phdThesis/phdThesis.pdf) table 8.5, p. 116 (We record
      // them in his form; to get the usual form, multiply by Exp(-\gamma+1/2) \sqrt{2} /
      // \pi.) He claims that they "guarantee 16 digit floating point accuracy in the
      // right-half plane", and since he uses fewer coefficients than Godfrey that would
      // be fantastic. But we measure relative deviations at the integers of 2, 0, 35, 23,
      // 45, 42, 6 X 10^(-16), making this relatively bad.
      //
      // Unfortunately, we didn't do these measurements ourselves at first, so we actually
      // used these coefficients until version 3.  Perhaps this really does give 53 bits
      // of accuracy if you do the calculation with more bits. The fact that G is
      // relatively large makes the initial coefficients relatively large, which probably
      // leads to cancellation errors.
      /*
      private const double LanczosG = 10.900511;

      private static readonly double[] LanczosC = double[] {
        +2.48574089138753565546e-5,
        1.05142378581721974210,
        -3.45687097222016235469,
        +4.51227709466894823700,
        -2.98285225323576655721,
        +1.05639711577126713077,
        -1.95428773191645869583e-1,
        +1.70970543404441224307e-2,
        -5.71926117404305781283e-4,
        +4.63399473359905636708e-6,
        -2.71994908488607703910e-9,
      };
      */

      // From LanczosG, we derive several values that we need only compute once.

      static constexpr double LanczosGP = LanczosG - 0.5;
      static constexpr double LanczosExpG = std::exp(-LanczosG);
      static constexpr double LanczosExpGP = std::exp(-LanczosGP);

    public:

      static double Sum (double x);
      static std::complex<double> Sum (std::complex<double> z);
      static double LogSumPrime (double x);
      static std::complex<double> LogSumPrime (std::complex<double> z);
      static double Gamma (double x);
      static double LogGamma (double x);
      static std::complex<double> LogGamma (std::complex<double> z);
      static double Psi (double x);
      static std::complex<double> Psi (std::complex<double> z);
      static double Beta (double x, double y);
      static double LogBeta (double x, double y);
  };

  std::complex<double> LogGamma_Stirling (std::complex<double> z);
  std::complex<double> LogGamma (std::complex<double> z);
  std::complex<double> Psi (std::complex<double> z);

  /// <summary>
  /// Contains a pair of solutions to a differential equation.
  /// </summary>
  /// <remarks>
  /// <para>Any linear second order differential equation has two independent solutions. For example,
  /// the Bessel differential equation (<see cref="AdvancedMath.Bessel"/>) has solutions J and Y,
  /// the Coulomb wave equation has solutions F and G,
  /// and the Airy differential equation has solutions Ai and Bi.</para>
  /// <para>A solution pair structure contains values for both solutions and for their derivatives. It is often useful to
  /// have all this information together when fitting boundary conditions.</para>
  /// <para>Which solution is considered the first and which is considered the second is
  /// a matter of convention. When one solution is regular (finite) at the origin and the other is not, we take the regular solution
  /// to be the first.</para>
  /// </remarks>
  class SolutionPair {

    private:

      double j, jPrime, y, yPrime;

      public:

      /// <summary>
      /// Gets the value of the first solution.
      /// </summary>
      inline double FirstSolutionValue() {
        return j;
      }

      inline void FirstSolutionValue(double value) {
        j = value;
      }

      /// <summary>
      /// Gets the derivative of the first solution.
      /// </summary>
      inline double FirstSolutionDerivative() {
        return jPrime;
      }

      inline void FirstSolutionDerivative(double value) {
        jPrime = value;
      }

      /// <summary>
      /// Gets the value of the second solution.
      /// </summary>
      inline double SecondSolutionValue() {
        return y;
      }

      inline void SecondSolutionValue(double value) {
        y = value;
      }

      /// <summary>
      /// Gets the derivative of the second solution.
      /// </summary>
      inline double SecondSolutionDerivative() {
        return (yPrime);
      }

      inline void SecondSolutionDerivative(double value) {
        yPrime = value;
      }

      // Leaving out the Wronskian for now because it can be subject to extreme cancelation error.
      /*
      /// <summary>
      /// Gets the Wronsikan of the solution pair.
      /// </summary>
      /// <remarks>
      /// <para>The Wronskian of a solution pair is the product of the first solution value and the second solution derivative minus the
      /// product of the second solution value and the first solution derivative.</para>
      /// </remarks>
      public double Wronskian {
        get {
          return (j * yPrime - y * jPrime);
        }
      }
      */

      SolutionPair() {
      }

      SolutionPair (double j, double jPrime, double y, double yPrime) {
        this->j = j;
        this->jPrime = jPrime;
        this->y = y;
        this->yPrime = yPrime;
      }
  };

  // Computes the length of a right triangle's hypotenuse.
  double Hypot (double x, double y);

  // for rho < turning point, CWF are exponential; for rho > turning point, CWF are oscilatory
  // we use this in several branching calculations
  double CoulombTurningPoint (double L, double eta);

  // The Gammow factor is the coefficient of the leading power of rho in the expansion of the CWF near the origin
  // It sets the order of magnitude of the function near the origin. Basically F ~ C, G ~ 1/C
  double CoulombFactorZero (double eta);
  double CoulombFactor (int L, double eta);

  // each term introduces factors of rho^2 / (L+1) and 2 eta rho / (L+1), so for this to converge we need
  // rho < sqrt(X) (1 + sqrt(L)) and 2 eta rho < X (1 + L); X ~ 16 gets convergence within 30 terms
  void CoulombF_Series (int L, double eta, double rho, double& F, double& FP);

  // series for L=0 for both F and G
  // this has the same convergence properties as the L != 0 series for F above
  void Coulomb_Zero_Series (double eta, double rho, double& F, double& FP, double& G, double& GP);

  // gives F'/F and sgn(F)
  // converges rapidly for rho < turning point; slowly for rho > turning point, but still converges
  double Coulomb_CF1 (double L, double eta, double rho, int& sign);

  // computes (G' + iF')/(G + i F)
  // converges quickly for rho > turning point; does not converge at all below it
  std::complex<double> Coulomb_CF2 (double L, double eta, double rho);

  // use Steed's method to compute F and G for a given L
  // the method uses a real continued fraction (1 constraint), an imaginary continued fraction (2 constraints)
  // and the Wronskian (4 constraints) to compute the 4 quantities F, F', G, G'
  // it is reliable past the truning point, but becomes slow if used far past the turning point
  SolutionPair Coulomb_Steed (double L, double eta, double rho);

  // asymptotic region
  void Coulomb_Asymptotic (double L, double eta, double rho, double& F, double& G);

  void Coulomb_Recurse_Upward (int L1, int L2, double eta, double rho, double& U, double& UP);

  double CoulombF_Integrate (int L, double eta, double rho);

  /// <summary>
  /// Computes the regular Coulomb wave function.
  /// </summary>
  /// <param name="L">The angular momentum number, which must be non-negative.</param>
  /// <param name="eta">The charge parameter, which can be postive or negative.</param>
  /// <param name="rho">The radial distance parameter, which must be non-negative.</param>
  /// <returns>The value of F<sub>L</sub>(&#x3B7;,&#x3C1;).</returns>
  /// <remarks>
  /// <para>The Coulomb wave functions are the radial wave functions of a non-relativistic particle in a Coulomb
  /// potential.</para>
  /// <para>They satisfy the differential equation:</para>
  /// <img src="../images/CoulombODE.png" />
  /// <para>A repulsive potential is represented by &#x3B7; &gt; 0, an attractive potential by &#x3B7; &lt; 0.</para>
  /// <para>F is oscilatory in the region beyond the classical turning point. In the quantum tunneling region inside
  /// the classical turning point, F is exponentially supressed.</para>
  /// <para>Many numerical libraries compute Coulomb wave functions in the quantum tunneling region using a WKB approximation,
  /// which accurately determine only the first handfull of digits; our library computes Coulomb wave functions even in this
  /// computationaly difficult region to nearly full precision -- all but the last 3-4 digits can be trusted.</para>
  /// <para>The irregular Coulomb wave functions G<sub>L</sub>(&#x3B7;,&#x3C1;) are the complementary independent solutions
  /// of the same differential equation.</para>
  /// </remarks>
  /// <exception cref="ArgumentOutOfRangeException"><paramref name="L"/> or <paramref name="rho"/> is negative.</exception>
  /// <seealso cref="CoulombG"/>
  /// <seealso href="http://en.wikipedia.org/wiki/Coulomb_wave_function" />
  /// <seealso href="http://mathworld.wolfram.com/CoulombWaveFunction.html" />
  double CoulombF (int L, double eta, double rho);

  /// <summary>
  /// Computes the irregular Coulomb wave function.
  /// </summary>
  /// <param name="L">The angular momentum number, which must be non-negative.</param>
  /// <param name="eta">The charge parameter, which can be postive or negative.</param>
  /// <param name="rho">The radial distance parameter, which must be non-negative.</param>
  /// <returns>The value of G<sub>L</sub>(&#x3B7;,&#x3C1;).</returns>
  /// <remarks>
  /// <para>For information on the Coulomb wave functions, see the remarks on <see cref="CoulombF" />.</para>
  /// </remarks>
  /// <exception cref="ArgumentOutOfRangeException"><paramref name="L"/> or <paramref name="rho"/> is negative.</exception>
  /// <seealso cref="CoulombF"/>
  /// <seealso href="http://en.wikipedia.org/wiki/Coulomb_wave_function" />
  /// <seealso href="http://mathworld.wolfram.com/CoulombWaveFunction.html" />
  double CoulombG (int L, double eta, double rho);

}
// -- End Ms-PL licensed code

//Microsoft Public License (Ms-PL)
//
//This license governs use of the accompanying software. If you use the software, you accept
//this license. If you do not accept the license, do not use the software.
//
//1. Definitions
//
//The terms "reproduce," "reproduction," "derivative works," and "distribution" have the
//same meaning here as under U.S. copyright law.
//
//A "contribution" is the original software, or any additions or changes to the software.
//
//A "contributor" is any person that distributes its contribution under this license.
//
//"Licensed patents" are a contributor's patent claims that read directly on its
//contribution.
//
//2. Grant of Rights
//
//(A) Copyright Grant- Subject to the terms of this license, including the license
//conditions and limitations in section 3, each contributor grants you a non-exclusive,
//worldwide, royalty-free copyright license to reproduce its contribution, prepare
//derivative works of its contribution, and distribute its contribution or any derivative
//works that you create.
//
//(B) Patent Grant- Subject to the terms of this license, including the license conditions
//and limitations in section 3, each contributor grants you a non-exclusive, worldwide,
//royalty-free license under its licensed patents to make, have made, use, sell, offer for
//sale, import, and/or otherwise dispose of its contribution in the software or derivative
//works of the contribution in the software.
//
//3. Conditions and Limitations
//
//(A) No Trademark License- This license does not grant you rights to use any contributors'
//name, logo, or trademarks.
//
//(B) If you bring a patent claim against any contributor over patents that you claim are
//infringed by the software, your patent license from such contributor to the software ends
//automatically.
//
//(C) If you distribute any portion of the software, you must retain all copyright, patent,
//trademark, and attribution notices that are present in the software.
//
//(D) If you distribute any portion of the software in source code form, you may do so only
//under this license by including a complete copy of this license with your distribution. If
//you distribute any portion of the software in compiled or object code form, you may only
//do so under a license that complies with this license.
//
//(E) The software is licensed "as-is." You bear the risk of using it. The contributors give
//no express warranties, guarantees or conditions. You may have additional consumer rights
//under your local laws which this license cannot change. To the extent permitted under your
//local laws, the contributors exclude the implied warranties of merchantability, fitness
//for a particular purpose and non-infringement.
