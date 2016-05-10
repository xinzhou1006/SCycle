#ifndef SbpOps_fc_coordTrans_COORDTRANS_H_INCLUDED
#define SbpOps_fc_coordTrans_COORDTRANS_H_INCLUDED

#include <petscksp.h>
#include <string>
#include <assert.h>
#include "domain.hpp"
#include "debuggingFuncs.hpp"
#include "spmat.hpp"
#include "sbpOps.hpp"

using namespace std;


/*
 * Constructs fully compatible linear SBP operators
 * with SAT boundary conditions:
 *
 *   D2^(mu) = -H^(-1) [ -D1^T H mu D1 - R + mu BD1]
 *
 */



/*
 * Container for matrices that are needed temporarily to construct
 * main operators. These include 1D SBP operators that are later mapped
 * to 2D, and the 2D factors that are used to enforce boundaries in the
 * A matrix.
 */
/*
 * Container for matrices that are needed temporarily to construct
 * main operators. These include 1D SBP operators that are later mapped
 * to 2D, and the 2D factors that are used to enforce boundaries in the
 * A matrix.
 */
struct TempMats_fc_coordTrans
    {
      const PetscInt    _order,_Ny,_Nz;
      const PetscReal   _dy,_dz;
      Mat               _mu;

      Spmat _Hy,_D1y,_D1yint,_Iy;
      Spmat _Hz,_D1z,_D1zint,_Iz;

      Mat _muxBSy_Iz;
      Mat _Hyinv_Iz;

      Mat _muxIy_BSz;
      Mat _Iy_Hzinv;

      Mat _AL;
      Mat _AR;
      Mat _AT;
      Mat _AB;

      Mat _H;

      TempMats_fc_coordTrans(const PetscInt order,const PetscInt Ny,
        const PetscScalar dy,const PetscInt Nz,const PetscScalar dz,Mat& mu);
      ~TempMats_fc_coordTrans();



    private:
      PetscErrorCode computeH();

      // disable default copy constructor and assignment operator
      TempMats_fc_coordTrans(const TempMats_fc_coordTrans & that);
      TempMats_fc_coordTrans& operator=( const TempMats_fc_coordTrans& rhs );
  };


/*
 * This class contains the summation-by-parts (SBP) matrices needed at
 * each time step to (1) compute the displacement in the medium, and (2) the
 * shear stress from the displacement. (1) Is accomplished by first
 * forming the vector rhs, which contains the boundary conditions, using
 * the function setRhs, and then using the matrix _A to compute the
 * displacement vector (uhat) from the linear equation A uhat = rhs.
 *
 * Note: PETSc's ability to count matrix creation/destructions is off.
 * For every MATAXPY, the number of destructions increments by 1 more than
 * the number of creations. Thus, after satBoundaries() the number will
 * be off by 4.
 */

class SbpOps_fc_coordTrans : public SbpOps
{

  private:

    const PetscInt    _order,_Ny,_Nz;
    const PetscReal   _dy,_dz;
    Vec              *_muVec;
    Mat              _mu;

    double _runTime;

    // type of problem to solve (associated BC matrices will be included as well):
    //   for Dyy+Dzz, input 'yz'
    //   for Dzz only, input 'z'
    //   for Dyy only, input 'y'
    string _type;

    // map boundary conditions to rhs vector
    string _bcTType,_bcRType,_bcBType,_bcLType; // options: displacement, traction
    Mat _rhsL,_rhsR,_rhsT,_rhsB;

    Mat _Hyinv_Iz,_Iy_Hzinv,_e0y_Iz,_eNy_Iz,_E0y_Iz,_ENy_Iz,_Iy_E0z,_Iy_ENz;


    // for coordinate transform
    Vec  _q,_r;
    Mat  _qy,_rz;


    // boundary conditions
    //~PetscScalar const _alphaF,_alphaR,_alphaS,_alphaD,_beta; // penalty terms
    PetscScalar _alphaT,_alphaDy,_alphaDz,_beta; // penalty terms for traction and displacement respectively

    // directory for matrix debugging
    string _debugFolder;


    PetscErrorCode constructH(const TempMats_fc_coordTrans& tempMats);
    PetscErrorCode constructHinv(const TempMats_fc_coordTrans& tempMats);
    PetscErrorCode construct1stDerivs(const TempMats_fc_coordTrans& tempMats);
    PetscErrorCode constructA(const TempMats_fc_coordTrans& tempMats);
    PetscErrorCode satBoundaries(TempMats_fc_coordTrans& tempMats);

    PetscErrorCode constructCoordTrans(TempMats_fc_coordTrans& tempMats);

    /*
     * Functions to compute intermediate matrices that comprise A:
     *     (second derivative in y) D2y = D2ymu + R2ymu
     *     (second derivative in z) D2z = D2zmu + R2zmu
     * where R2ymu and R2zmu vanish as the grid spacing approaches 0.
     */
    PetscErrorCode constructD2ymu(const TempMats_fc_coordTrans& tempMats, Mat &D2ymu);
    PetscErrorCode constructD2zmu(const TempMats_fc_coordTrans& tempMats, Mat &D2zmu);
    PetscErrorCode constructRymu(const TempMats_fc_coordTrans& tempMats,Mat &Rymu);
    PetscErrorCode constructRzmu(const TempMats_fc_coordTrans& tempMats,Mat &Rzmu);


    // disable default copy constructor and assignment operator
    SbpOps_fc_coordTrans(const SbpOps_fc_coordTrans & that);
    SbpOps_fc_coordTrans& operator=( const SbpOps_fc_coordTrans& rhs );

  public:

    Mat _H;
    Mat _Hinv;
    Mat _A;
    Mat _Dy_Iz, _Iy_Dz;

    //~SbpOps_fc_coordTrans(Domain&D,PetscScalar& muArr,Mat& mu);
    SbpOps_fc_coordTrans(Domain&D,Vec& muVec,string bcT,string bcR,string bcB, string bcL, string type);
    ~SbpOps_fc_coordTrans();

    // create the vector rhs out of the boundary conditions (_bc*)
    PetscErrorCode setRhs(Vec&rhs,Vec &bcL,Vec &bcR,Vec &bcT,Vec &bcB);

    // read/write commands
    PetscErrorCode loadOps(const std::string inputDir);
    PetscErrorCode writeOps(const std::string outputDir);

    PetscErrorCode getA(Mat &mat);


    // functions to compute various derivatives of input vectors (this
    // will allow the matrix-free version of these operators to present
    // the exact same interface to the as the matrix version).
    PetscErrorCode Dy(const Vec &in, Vec &out); // out = Dy * in
    PetscErrorCode muxDy(const Vec &in, Vec &out); // out = mu * Dy * in
    PetscErrorCode Dyxmu(const Vec &in, Vec &out); // out = Dy * mu * in
    PetscErrorCode Dz(const Vec &in, Vec &out); // out = Dz * in
    PetscErrorCode muxDz(const Vec &in, Vec &out); // out = mu * Dz * in
    PetscErrorCode Dzxmu(const Vec &in, Vec &out); // out = Dz * mu * in

    PetscErrorCode H(const Vec &in, Vec &out); // out = H * in
    PetscErrorCode Hinv(const Vec &in, Vec &out); // out = H * in
    PetscErrorCode Hyinvxe0y(const Vec &in, Vec &out); // out = Hy^-1 * e0y * in
    PetscErrorCode HyinvxeNy(const Vec &in, Vec &out); // out = Hy^-1 * eNy * in
    PetscErrorCode HyinvxE0y(const Vec &in, Vec &out); // out = Hy^-1 * E0y * in
    PetscErrorCode HyinvxENy(const Vec &in, Vec &out); // out = Hy^-1 * ENy * in
    PetscErrorCode HzinvxE0z(const Vec &in, Vec &out); // out = Hz^-1 * e0z * in
    PetscErrorCode HzinvxENz(const Vec &in, Vec &out); // out = Hz^-1 * eNz * in
};

// functions to construct 1D sbp operators
PetscErrorCode sbp_fc_coordTrans_Spmat(const PetscInt order,const PetscInt N,const PetscScalar scale,
                        Spmat& H,Spmat& Hinv,Spmat& D1,Spmat& D1int, Spmat& S);
PetscErrorCode sbp_fc_coordTrans_Spmat2(const PetscInt N,const PetscScalar scale,Spmat& D2,Spmat& C2);
PetscErrorCode sbp_fc_coordTrans_Spmat4(const PetscInt N,const PetscScalar scale,
                         Spmat& D3, Spmat& D4, Spmat& C3, Spmat& C4);

#endif
