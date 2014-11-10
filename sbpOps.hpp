#ifndef SBPOPS_H_INCLUDED
#define SBPOPS_H_INCLUDED

#include <petscksp.h>
#include <string>
#include <assert.h>
#include "domain.hpp"
#include "debuggingFuncs.hpp"
#include "spmat.hpp"

using namespace std;

class SbpOps
{

  private:

    const PetscInt    _order,_Ny,_Nz;
    const PetscReal   _dy,_dz;
    PetscScalar      *_muArr;
    Mat              *_mu;

    double _runTime;

    // map boundary conditions to rhs vector
    Mat _rhsL,_rhsR,_rhsT,_rhsB;
    Mat _AL,_AR,_AT,_AB;

    // SBP factors
    //~PetscScalar *_HinvyArr,*_D1y,*_D1yint,*_D2y,*_SyArr;
    //~PetscScalar *_HinvzArr,*_D1z,*_D1zint,*_D2z,*_SzArr;
    //~PetscInt _Sylen,_Szlen;

    // Spmats holding 1D SBP operators (temporarily named with extraneous S's)
    // needed for all orders
    Spmat _Hy,_Hyinv,_D1y,_D1yint,_D2y,_Sy,_Iy;
    Spmat _Hz,_Hzinv,_D1z,_D1zint,_D2z,_Sz,_Iz;

    // boundary conditions
    PetscScalar const _alphaF,_alphaR,_alphaS,_alphaD,_beta; // penalty terms

    // directory for matrix debugging
    string _debugFolder;


    PetscErrorCode computeDy_Iz();
    PetscErrorCode computeA();
    PetscErrorCode satBoundaries();

    PetscErrorCode sbpSpmat(const PetscInt N,const PetscScalar scale,Spmat& H,Spmat& Hinv,Spmat& D1,
                 Spmat& D1int, Spmat& D2, Spmat& S);
    PetscErrorCode sbpSpmat4(const PetscInt N,const PetscScalar scale,
                Spmat& D3, Spmat& D4, Spmat& C3, Spmat& C4);

    PetscErrorCode sbpArrays(const PetscInt N,const PetscScalar scale,PetscScalar *Hinv,
                             PetscScalar *D1,PetscScalar *D1int,PetscScalar *D2,
                             PetscScalar *S,PetscInt *Slen);
    PetscErrorCode computeD2ymu(Mat &D2ymu);
    PetscErrorCode computeD2zmu(Mat &D2zmu);
    PetscErrorCode computeRymu(Mat &Rymu,PetscInt order);
    PetscErrorCode computeRzmu(Mat &Rzmu, PetscInt order);


    // disable default copy constructor and assignment operator
    SbpOps(const SbpOps & that);
    SbpOps& operator=( const SbpOps& rhs );

  public:

    Mat _A;
    Mat _Dy_Iz;
    Mat _H;
    //~Vec _rhs;

    SbpOps(Domain&D);
    ~SbpOps();

    //~PetscErrorCode setSystem();
    PetscErrorCode setRhs(Vec&rhs,Vec &_bcF,Vec &_bcR,Vec &_bcS,Vec &_bcD);
    PetscErrorCode computeH();

    // read/write commands
    PetscErrorCode loadOps(const std::string inputDir);
    PetscErrorCode writeOps(const std::string outputDir);

    // visualization
    //~PetscErrorCode printMyArray(PetscScalar *myArray, PetscInt N);
    //~PetscErrorCode viewSBP();


};

#endif
