#include "linearElastic.hpp"


using namespace std;


LinearElastic::LinearElastic(Domain&D)
: _order(D._order),_Ny(D._Ny),_Nz(D._Nz),
  _Ly(D._Ly),_Lz(D._Lz),_dy(_Ly/(_Ny-1.)),_dz(_Lz/(_Nz-1.)),
  _problemType(D._problemType),_outputDir(D._outputDir),
  _v0(D._v0),_vL(D._vp),
  _muArrPlus(D._muArrPlus),_muPlus(D._muPlus),
  _bcRPlusShift(NULL),_surfDispPlus(NULL),
  _rhsPlus(NULL),_uPlus(NULL),_sigma_xyPlus(NULL),
  _linSolver(D._linSolver),_kspPlus(NULL),_pcPlus(NULL),
  _kspTol(D._kspTol),_sbpPlus(D,*D._muArrPlus,D._muPlus),
  _timeIntegrator(D._timeIntegrator),
  _strideLength(D._strideLength),_maxStepCount(D._maxStepCount),
  _initTime(D._initTime),_currTime(_initTime),_maxTime(D._maxTime),
  _minDeltaT(D._minDeltaT),_maxDeltaT(D._maxDeltaT),
  _stepCount(0),_atol(D._atol),_initDeltaT(D._initDeltaT),
  _timeViewer(NULL),_surfDispPlusViewer(NULL),
  _integrateTime(0),_writeTime(0),_linSolveTime(0),_factorTime(0),_linSolveCount(0),
  _bcTPlus(NULL),_bcRPlus(NULL),_bcBPlus(NULL),_bcLPlus(NULL)
  //_fault(NULL)
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting LinearElastic::LinearElastic in lithosphere.cpp.\n");
#endif

  // boundary conditions
  VecCreate(PETSC_COMM_WORLD,&_bcLPlus);
  VecSetSizes(_bcLPlus,PETSC_DECIDE,_Nz);
  VecSetFromOptions(_bcLPlus);     PetscObjectSetName((PetscObject) _bcLPlus, "_bcLPlus");
  VecSet(_bcLPlus,0.0);

  VecDuplicate(_bcLPlus,&_bcRPlusShift); PetscObjectSetName((PetscObject) _bcRPlusShift, "bcRplusShift");
  VecDuplicate(_bcLPlus,&_bcRPlus); PetscObjectSetName((PetscObject) _bcRPlus, "bcRplus");
  VecSet(_bcRPlus,_vL*_initTime/2.0);

  VecCreate(PETSC_COMM_WORLD,&_bcTPlus);
  VecSetSizes(_bcTPlus,PETSC_DECIDE,_Ny);
  VecSetFromOptions(_bcTPlus);     PetscObjectSetName((PetscObject) _bcTPlus, "_bcTPlus");
  VecSet(_bcTPlus,0.0);

  VecDuplicate(_bcTPlus,&_bcBPlus); PetscObjectSetName((PetscObject) _bcBPlus, "_bcBPlus");
  VecSet(_bcBPlus,0.0);

  KSPCreate(PETSC_COMM_WORLD,&_kspPlus);
  setupKSP(_sbpPlus,_kspPlus,_pcPlus);

  VecCreate(PETSC_COMM_WORLD,&_rhsPlus);
  VecSetSizes(_rhsPlus,PETSC_DECIDE,_Ny*_Nz);
  VecSetFromOptions(_rhsPlus);



  VecDuplicate(_bcTPlus,&_surfDispPlus); PetscObjectSetName((PetscObject) _surfDispPlus, "_surfDispPlus");

  if (_timeIntegrator.compare("FEuler")==0) {
    _quadrature = new FEuler(_maxStepCount,_maxTime,_initDeltaT,D._timeControlType);
  }
  else if (_timeIntegrator.compare("RK32")==0) {
    _quadrature = new RK32(_maxStepCount,_maxTime,_initDeltaT,D._timeControlType);
  }
  else {
    PetscPrintf(PETSC_COMM_WORLD,"ERROR: timeIntegrator type type not understood\n");
    assert(0>1); // automatically fail
  }

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending LinearElastic::LinearElastic in lithosphere.cpp.\n");
#endif
}


LinearElastic::~LinearElastic()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting LinearElastic::~LinearElastic in lithosphere.cpp.\n");
#endif

  // boundary conditions
  VecDestroy(&_bcLPlus);
  VecDestroy(&_bcRPlus);
  VecDestroy(&_bcTPlus);
  VecDestroy(&_bcBPlus);

  // body fields
  VecDestroy(&_rhsPlus);
  VecDestroy(&_uPlus);
  VecDestroy(&_sigma_xyPlus);
  VecDestroy(&_surfDispPlus);

  KSPDestroy(&_kspPlus);

  PetscViewerDestroy(&_timeViewer);
  PetscViewerDestroy(&_surfDispPlusViewer);


#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending LinearElastic::~LinearElastic in lithosphere.cpp.\n");
#endif
}


/*
 * Set up the Krylov Subspace and Preconditioner (KSP) environment. A
 * table of options available through PETSc and linked external packages
 * is available at
 * http://www.mcs.anl.gov/petsc/documentation/linearsolvertable.html.
 *
 * The methods implemented here are:
 *     Algorithm             Package           input file syntax
 * algebraic multigrid       HYPRE                AMG
 * direct LU                 MUMPS                MUMPSLU
 * direct Cholesky           MUMPS                MUMPSCHOLESKY
 *
 * A list of options for each algorithm that can be set can be optained
 * by running the code with the argument main <input file> -help and
 * searching through the output for "Preconditioner (PC) options" and
 * "Krylov Method (KSP) options".
 *
 * To view convergence information, including number of iterations, use
 * the command line argument: -ksp_converged_reason.
 *
 * For information regarding HYPRE's solver options, especially the
 * preconditioner options, use the User manual online. Also, use -ksp_view.
 */
PetscErrorCode LinearElastic::setupKSP(SbpOps& sbp,KSP& ksp,PC& pc)
{
  PetscErrorCode ierr = 0;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting LinearElastic::setupKSP in lithosphere.cpp\n");CHKERRQ(ierr);
#endif

  //~ierr = KSPSetType(_ksp,KSPGMRES);CHKERRQ(ierr);
  //~ierr = KSPSetOperators(_ksp,_A,_A,SAME_PRECONDITIONER);CHKERRQ(ierr);
  //~ierr = KSPGetPC(_ksp,&_pc);CHKERRQ(ierr);


  // use PETSc's direct LU - only available on 1 processor!!!
  //~ierr = PCSetType(D.pc,PCLU);CHKERRQ(ierr);

  if (_linSolver.compare("AMG")==0) { // algebraic multigrid from HYPRE
    // uses HYPRE's solver AMG (not HYPRE's preconditioners)
    ierr = KSPSetType(ksp,KSPRICHARDSON);CHKERRQ(ierr);
    ierr = KSPSetOperators(ksp,sbp._A,sbp._A,SAME_PRECONDITIONER);CHKERRQ(ierr);
    ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
    ierr = PCSetType(pc,PCHYPRE);CHKERRQ(ierr);
    ierr = PCHYPRESetType(pc,"boomeramg");CHKERRQ(ierr);
    ierr = KSPSetTolerances(ksp,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT);CHKERRQ(ierr);
    ierr = PCFactorSetLevels(pc,4);CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(ksp,PETSC_TRUE);CHKERRQ(ierr);
  }
  else if (_linSolver.compare("PCG")==0) { // preconditioned conjugate gradient
    // use built in preconditioned conjugate gradient
    ierr = KSPSetType(ksp,KSPCG);CHKERRQ(ierr);
    ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);

    // preconditioned with PCGAMG
    PCSetType(pc,PCGAMG); // default?


    // preconditioned with HYPRE's AMG
    //~PCSetType(pc,PCHYPRE);
    //~ierr = PCHYPRESetType(pc,"boomeramg");CHKERRQ(ierr);
    //~ierr = PCFactorSetLevels(pc,3);CHKERRQ(ierr);

    // preconditioned with HYPRE
    //~PCSetType(pc,PCHYPRE);
    //~ierr = PCHYPRESetType(pc,"euclid");CHKERRQ(ierr);
    //~ierr = PetscOptionsSetValue("-pc_hypre_euclid_levels","1");CHKERRQ(ierr); // this appears to be fastest


    ierr = KSPSetOperators(ksp,sbp._A,sbp._A,SAME_PRECONDITIONER);CHKERRQ(ierr);
    ierr = KSPSetTolerances(ksp,_kspTol,PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT);CHKERRQ(ierr);
    ierr = KSPSetInitialGuessNonzero(ksp,PETSC_TRUE);CHKERRQ(ierr);
  }
  else if (_linSolver.compare("MUMPSLU")==0) { // direct LU from MUMPS
    // use direct LU from MUMPS
    ierr = KSPSetType(ksp,KSPPREONLY);CHKERRQ(ierr);
    ierr = KSPSetOperators(ksp,sbp._A,sbp._A,SAME_PRECONDITIONER);CHKERRQ(ierr);
    ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
    PCSetType(pc,PCLU);
    PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS);
    PCFactorSetUpMatSolverPackage(pc);
  }

  else if (_linSolver.compare("MUMPSCHOLESKY")==0) { // direct Cholesky (RR^T) from MUMPS
    // use direct LL^T (Cholesky factorization) from MUMPS
    ierr = KSPSetType(ksp,KSPPREONLY);CHKERRQ(ierr);
    ierr = KSPSetOperators(ksp,sbp._A,sbp._A,SAME_PRECONDITIONER);CHKERRQ(ierr);
    ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
    PCSetType(pc,PCCHOLESKY);
    PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS);
    PCFactorSetUpMatSolverPackage(pc);
  }
  else {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"ERROR: linSolver type not understood\n");
    assert(0>1);
  }

  // finish setting up KSP context using options defined above
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);

  // perform computation of preconditioners now, rather than on first use
  ierr = KSPSetUp(ksp);CHKERRQ(ierr);



#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending LinearElastic::setupKSP in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}

/*
PetscErrorCode LinearElastic::integrate()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting LinearElastic::integrate in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  double startTime = MPI_Wtime();

  // call odeSolver routine integrate here
  _quadrature->setTolerance(_atol);CHKERRQ(ierr);
  _quadrature->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
  ierr = _quadrature->setTimeRange(_initTime,_maxTime);
  ierr = _quadrature->setInitialConds(_fault._var);CHKERRQ(ierr);

  ierr = _quadrature->integrate(this);CHKERRQ(ierr);
  _integrateTime += MPI_Wtime() - startTime;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending LinearElastic::integrate in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}*/




PetscErrorCode LinearElastic::timeMonitor(const PetscReal time,const PetscInt stepCount,
                             const_it_vec varBegin,const_it_vec varEnd,
                             const_it_vec dvarBegin,const_it_vec dvarEnd)
{
  PetscErrorCode ierr = 0;

  if ( stepCount % _strideLength == 0) {
    _stepCount++;
    _currTime = time;
    //~ierr = PetscViewerHDF5IncrementTimestep(D->viewer);CHKERRQ(ierr);
    ierr = writeStep();CHKERRQ(ierr);
  }

#if VERBOSE > 0
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %.15e\n",stepCount,_currTime);CHKERRQ(ierr);
#endif
  return ierr;
}

PetscErrorCode LinearElastic::view()
{
  PetscErrorCode ierr = 0;
  ierr = _quadrature->view();
  ierr = PetscPrintf(PETSC_COMM_WORLD,"\n-------------------------------\n\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Runtime Summary:\n");CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent in integration (s): %g\n",_integrateTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent writing output (s): %g\n",_writeTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent setting up linear solve context (e.g. factoring) (s): %g\n",_factorTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   number of times linear system was solved: %i\n",_linSolveCount);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"   time spent solving linear system (s): %g\n",_linSolveTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);

  //~ierr = KSPView(_kspPlus,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  return ierr;
}











//================= Symmetric LinearElastic Functions ========================

SymmLinearElastic::SymmLinearElastic(Domain&D)
: LinearElastic(D),_fault(D)
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting SymmLinearElastic::SymmLinearElastic in lithosphere.cpp.\n");
#endif

  //~_fault = new SymmFault(D);

  // almost everything is covered by base class' constructor, except the
  // construction of _fault, and populating bcRshift

  setShifts(); // set _bcRPlusShift
  VecAXPY(_bcRPlus,1.0,_bcRPlusShift);


  _sbpPlus.setRhs(_rhsPlus,_bcLPlus,_bcRPlus,_bcTPlus,_bcBPlus);

  VecDuplicate(_rhsPlus,&_uPlus);
  double startTime = MPI_Wtime();
  KSPSolve(_kspPlus,_rhsPlus,_uPlus);
  _factorTime += MPI_Wtime() - startTime;

  VecDuplicate(_rhsPlus,&_sigma_xyPlus);
  MatMult(_sbpPlus._muxDy_Iz,_uPlus,_sigma_xyPlus);



  //~// for SS approximation
  //~VecDuplicate(_rhsPlus,&_sigma_xyPlus);
  //~VecSet(_sigma_xyPlus,0);
  //~VecCopy(_bcRPlus,_sigma_xyPlus);
  //~VecAXPY(_sigma_xyPlus,-1,_bcLPlus);
  //~VecScale(_sigma_xyPlus,_muArrPlus[1]/_Ly);

  _fault.setTauQS(_sigma_xyPlus,NULL);
  _fault.setFaultDisp(_bcLPlus,NULL);
  _fault.computeVel();

  //~setSurfDisp();

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending SymmLinearElastic::SymmLinearElastic in lithosphere.cpp.\n");
#endif
}

SymmLinearElastic::~SymmLinearElastic(){};



PetscErrorCode SymmLinearElastic::computeShearStress()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmLinearElastic::computeShearStress in lithosphere.cpp.\n");CHKERRQ(ierr);
#endif

  ierr = MatMult(_sbpPlus._muxDy_Iz,_uPlus,_sigma_xyPlus);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmLinearElastic::computeShearStress in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}

// destructor is covered by base class



PetscErrorCode SymmLinearElastic::setShifts()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmLinearElastic::setShifts in lithosphere.cpp\n");CHKERRQ(ierr);
#endif


  PetscInt Ii,Istart,Iend;
  PetscScalar v,bcRshift;
  ierr = VecGetOwnershipRange(_bcRPlusShift,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    v = _fault.getTauInf(Ii);
    bcRshift = 0.8*  v*_Ly/_muArrPlus[_Ny*_Nz-_Nz+Ii]; // use last values of muArr
    //~bcRshift = v*_Ly/_muArrPlus[Ii]; // use first values of muArr
    //~bcRshift = 0.;
    ierr = VecSetValue(_bcRPlusShift,Ii,bcRshift,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_bcRPlusShift);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcRPlusShift);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmLinearElastic::setShifts in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}




PetscErrorCode SymmLinearElastic::setSurfDisp()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmLinearElastic::setSurfDisp in lithosphere.cpp\n");CHKERRQ(ierr);
#endif

  PetscInt    Ii,Istart,Iend;
  PetscScalar u,y,z;
  ierr = VecGetOwnershipRange(_uPlus,&Istart,&Iend);
  for (Ii=Istart;Ii<Iend;Ii++) {
    z = Ii-_Nz*(Ii/_Nz);
    y = Ii/_Nz;
    if (z == 0) {
      ierr = VecGetValues(_uPlus,1,&Ii,&u);CHKERRQ(ierr);
      ierr = VecSetValue(_surfDispPlus,y,u,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_surfDispPlus);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_surfDispPlus);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmLinearElastic::setSurfDisp in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}



PetscErrorCode SymmLinearElastic::writeStep()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmLinearElastic::writeStep in lithosphere.cpp at step %i\n",_stepCount);CHKERRQ(ierr);
#endif
  double startTime = MPI_Wtime();

  if (_stepCount==0) {
    ierr = _sbpPlus.writeOps(_outputDir);CHKERRQ(ierr);
    ierr = _fault.writeContext(_outputDir);CHKERRQ(ierr);
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(_outputDir+"time.txt").c_str(),&_timeViewer);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispPlus").c_str(),FILE_MODE_WRITE,
                                 &_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_surfDispPlus,_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispPlus").c_str(),
                                   FILE_MODE_APPEND,&_surfDispPlusViewer);CHKERRQ(ierr);

    //~// boundary conditions
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcRPlus").c_str(),FILE_MODE_WRITE,
                                 &_bcRPlusV);CHKERRQ(ierr);
    ierr = VecView(_bcRPlus,_bcRPlusV);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_bcRPlusV);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcRPlus").c_str(),
                                   FILE_MODE_APPEND,&_bcRPlusV);CHKERRQ(ierr);
//~
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcRplusShift").c_str(),FILE_MODE_WRITE,
                                 //~&_bcRPlusShiftV);CHKERRQ(ierr);
    //~ierr = VecView(_bcRPlusShift,_bcRPlusShiftV);CHKERRQ(ierr);
    //~ierr = PetscViewerDestroy(&_bcRPlusShiftV);CHKERRQ(ierr);
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcRplusShift").c_str(),
                                   //~FILE_MODE_APPEND,&_bcRPlusShiftV);CHKERRQ(ierr);
//~
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcLPlus").c_str(),FILE_MODE_WRITE,
                                 //~&_bcLPlusV);CHKERRQ(ierr);
    //~ierr = VecView(_bcLPlus,_bcLPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerDestroy(&_bcLPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"bcLPlus").c_str(),
                                   //~FILE_MODE_APPEND,&_bcLPlusV);CHKERRQ(ierr);
//~
    //~// body fields
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"uBodyPlus").c_str(),FILE_MODE_WRITE,
                                 //~&_uPlusV);CHKERRQ(ierr);
    //~ierr = VecView(_uPlus,_uPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerDestroy(&_uPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"uBodyPlus").c_str(),
                                   //~FILE_MODE_APPEND,&_uPlusV);CHKERRQ(ierr);
//~
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"sigmaXYplus").c_str(),FILE_MODE_WRITE,
                                 //~&_sigma_xyPlusV);CHKERRQ(ierr);
    //~ierr = VecView(_sigma_xyPlus,_sigma_xyPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerDestroy(&_sigma_xyPlusV);CHKERRQ(ierr);
    //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"sigmaXYplus").c_str(),
                                   //~FILE_MODE_APPEND,&_sigma_xyPlusV);CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_surfDispPlus,_surfDispPlusViewer);CHKERRQ(ierr);

    ierr = VecView(_bcRPlus,_bcRPlusV);CHKERRQ(ierr);
    //~ierr = VecView(_bcRPlusShift,_bcRPlusShiftV);CHKERRQ(ierr);

    //~ierr = VecView(_uPlus,_uPlusV);CHKERRQ(ierr);
    //~ierr = VecView(_sigma_xyPlus,_sigma_xyPlusV);CHKERRQ(ierr);
  }
  ierr = _fault.writeStep(_outputDir,_stepCount);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(_timeViewer, "%.15e\n",_currTime);CHKERRQ(ierr);

  _writeTime += MPI_Wtime() - startTime;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmLinearElastic::writeStep in lithosphere.cpp at step %i\n",_stepCount);CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode SymmLinearElastic::integrate()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting LinearElastic::integrate in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  double startTime = MPI_Wtime();

  // call odeSolver routine integrate here
  _quadrature->setTolerance(_atol);CHKERRQ(ierr);
  _quadrature->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
  ierr = _quadrature->setTimeRange(_initTime,_maxTime);
  ierr = _quadrature->setInitialConds(_fault._var);CHKERRQ(ierr);

  ierr = _quadrature->integrate(this);CHKERRQ(ierr);
  _integrateTime += MPI_Wtime() - startTime;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending LinearElastic::integrate in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}



PetscErrorCode SymmLinearElastic::d_dt(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                 it_vec dvarBegin,it_vec dvarEnd)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmLinearElastic::d_dt in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif

  // update boundaries
  ierr = VecCopy(*(varBegin+1),_bcLPlus);CHKERRQ(ierr);
  ierr = VecScale(_bcLPlus,0.5);CHKERRQ(ierr); // var holds slip velocity, bcL is displacement at y=0+
  ierr = VecSet(_bcRPlus,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcRPlus,1.0,_bcRPlusShift);CHKERRQ(ierr);

  // solve for displacement
  ierr = _sbpPlus.setRhs(_rhsPlus,_bcLPlus,_bcRPlus,_bcTPlus,_bcBPlus);CHKERRQ(ierr);
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspPlus,_rhsPlus,_uPlus);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;
  ierr = setSurfDisp();

  // solve for shear stress
  ierr = MatMult(_sbpPlus._muxDy_Iz,_uPlus,_sigma_xyPlus);CHKERRQ(ierr);

  //~// for SS approximation
  //~VecCopy(_bcRPlus,_sigma_xyPlus);
  //~VecAXPY(_sigma_xyPlus,-1,_bcLPlus);
  //~VecScale(_sigma_xyPlus,_muArrPlus[0]/_Ly);


  // update fields on fault
  ierr = _fault.setTauQS(_sigma_xyPlus,NULL);CHKERRQ(ierr);
  ierr = _fault.d_dt(varBegin,varEnd, dvarBegin, dvarEnd);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmLinearElastic::d_dt in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif
  return ierr;
}



// Outputs data at each time step.
PetscErrorCode SymmLinearElastic::debug(const PetscReal time,const PetscInt stepCount,
                     const_it_vec varBegin,const_it_vec varEnd,
                     const_it_vec dvarBegin,const_it_vec dvarEnd,const char *stage)
{
  PetscErrorCode ierr = 0;

#if ODEPRINT > 0
  PetscInt       Istart,Iend;
  PetscScalar    bcRval,uVal,psiVal,velVal,dQVal,tauQS;

  //~PetscScalar k = _muArrPlus[0]/2/_Ly;

  ierr= VecGetOwnershipRange(*varBegin,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(*varBegin,1,&Istart,&psiVal);CHKERRQ(ierr);

  ierr = VecGetValues(*(varBegin+1),1,&Istart,&uVal);CHKERRQ(ierr);

  ierr= VecGetOwnershipRange(*dvarBegin,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(*dvarBegin,1,&Istart,&dQVal);CHKERRQ(ierr);
  ierr = VecGetValues(*(dvarBegin+1),1,&Istart,&velVal);CHKERRQ(ierr);

  ierr= VecGetOwnershipRange(_bcRPlus,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRPlus,1,&Istart,&bcRval);CHKERRQ(ierr);

  ierr = VecGetValues(_fault._tauQSPlus,1,&Istart,&tauQS);CHKERRQ(ierr);

  if (stepCount == 0) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"%-4s %-6s | %-15s %-15s %-15s | %-15s %-15s %-16s | %-15s\n",
                       "Step","Stage","bcR","D","Q","tauQS","V","dQ","time");
    CHKERRQ(ierr);
  }
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%4i %-6s ",stepCount,stage);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD," | %.9e %.9e %.9e ",bcRval,uVal,psiVal);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD," | %.9e %.9e %.9e ",tauQS,velVal,dQVal);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD," | %.9e\n",time);CHKERRQ(ierr);



  //~VecView(_fault._tauQSPlus,PETSC_VIEWER_STDOUT_WORLD);
#endif
  return ierr;
}





//================= Full LinearElastic (+ and - sides) Functions =========
FullLinearElastic::FullLinearElastic(Domain&D)
: LinearElastic(D),
  _muArrMinus(D._muArrMinus),_muMinus(D._muMinus),
  _bcLMinusShift(NULL),_surfDispMinus(NULL),
  _rhsMinus(NULL),_uMinus(NULL),_sigma_xyMinus(NULL),
  _surfDispMinusViewer(NULL),
  _kspMinus(NULL),_pcMinus(NULL),
  _sbpMinus(D,*D._muArrMinus,D._muMinus),
  _bcTMinus(NULL),_bcRMinus(NULL),_bcBMinus(NULL),_bcLMinus(NULL),
  _fault(D)
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::FullLinearElastic in lithosphere.cpp.\n");
#endif

  //~_fault = new FullFault(D);

  // initialize y<0 boundary conditions
  VecDuplicate(_bcLPlus,&_bcLMinusShift);
  PetscObjectSetName((PetscObject) _bcLMinusShift, "_bcLMinusShift");
  setShifts(); // set position of boundary from steady sliding
  VecAXPY(_bcRPlus,1.0,_bcRPlusShift);


  // fault initial displacement on minus side
  VecDuplicate(_bcLPlus,&_bcRMinus); PetscObjectSetName((PetscObject) _bcRMinus, "_bcRMinus");
  VecSet(_bcRMinus,0.0);

  // remote displacement on - side
  VecDuplicate(_bcLPlus,&_bcLMinus); PetscObjectSetName((PetscObject) _bcLMinus, "bcLMinus");
  VecSet(_bcLMinus,-_vL*_initTime/2.0);
  VecAXPY(_bcLMinus,1.0,_bcLMinusShift);

  VecDuplicate(_bcTPlus,&_bcTMinus); PetscObjectSetName((PetscObject) _bcTMinus, "bcTMinus");
  VecSet(_bcTMinus,0.0);
  VecDuplicate(_bcBPlus,&_bcBMinus); PetscObjectSetName((PetscObject) _bcBMinus, "bcBMinus");
  VecSet(_bcBMinus,0.0);

  // initialize and allocate memory for body fields
  double startTime;

  VecDuplicate(_rhsPlus,&_uPlus);
  VecDuplicate(_rhsPlus,&_sigma_xyPlus);

  VecCreate(PETSC_COMM_WORLD,&_rhsMinus);
  VecSetSizes(_rhsMinus,PETSC_DECIDE,_Ny*_Nz);
  VecSetFromOptions(_rhsMinus);
  VecDuplicate(_rhsMinus,&_uMinus);
  VecDuplicate(_rhsMinus,&_sigma_xyMinus);


  // initialize KSP for y<0
  KSPCreate(PETSC_COMM_WORLD,&_kspMinus);
  startTime = MPI_Wtime();
  setupKSP(_sbpMinus,_kspMinus,_pcMinus);
  _factorTime += MPI_Wtime() - startTime;


  // solve for displacement and shear stress in y<0
  _sbpMinus.setRhs(_rhsMinus,_bcLMinus,_bcRMinus,_bcTMinus,_bcBMinus);
  KSPSolve(_kspMinus,_rhsMinus,_uMinus);
  MatMult(_sbpMinus._muxDy_Iz,_uMinus,_sigma_xyMinus);

  // solve for displacement and shear stress in y>0
  _sbpPlus.setRhs(_rhsPlus,_bcLPlus,_bcRPlus,_bcTPlus,_bcBPlus);
  startTime = MPI_Wtime();
  KSPSolve(_kspPlus,_rhsPlus,_uPlus);
  _factorTime += MPI_Wtime() - startTime;
  MatMult(_sbpPlus._muxDy_Iz,_uPlus,_sigma_xyPlus);


  //~setU(); // set _uPlus, _uMinus analytically
  //~setSigmaxy(); // set shear stresses analytically


  //~PetscPrintf(PETSC_COMM_WORLD,"_bcLMinus = \n");
  //~printVec(_bcLMinus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_bcRMinus = \n");
  //~printVec(_bcRMinus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_bcLPlus = \n");
  //~printVec(_bcLPlus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_bcRPlus = \n");
  //~printVec(_bcRPlus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_uPlus = \n");
  //~printVec(_uPlus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_uMinus = \n");
  //~printVec(_uMinus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_sigma_xyMinus = \n");
  //~printVec(_sigma_xyMinus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_sigma_xyPlus = \n");
  //~printVec(_sigma_xyPlus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_sigma_xy diff = \n");
  //~printVecsDiff(_sigma_xyPlus,_sigma_xyMinus);
  //~assert(0>1);


  // set up fault
  _fault.setTauQS(_sigma_xyPlus,_sigma_xyMinus);
  _fault.setFaultDisp(_bcLPlus,_bcRMinus);
  _fault.computeVel();


  VecDuplicate(_bcTMinus,&_surfDispMinus); PetscObjectSetName((PetscObject) _surfDispMinus, "_surfDispMinus");
  setSurfDisp(); // extract surface displacement from displacement fields

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::FullLinearElastic in lithosphere.cpp.\n");
#endif
}

FullLinearElastic::~FullLinearElastic()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::~FullLinearElastic in lithosphere.cpp.\n");
#endif

  // boundary conditions: minus side
  VecDestroy(&_bcLMinus);
  VecDestroy(&_bcLMinusShift);
  VecDestroy(&_bcRMinus);
  VecDestroy(&_bcTMinus);
  VecDestroy(&_bcBMinus);

  // body fields
  VecDestroy(&_rhsMinus);
  VecDestroy(&_uMinus);
  VecDestroy(&_sigma_xyMinus);

  VecDestroy(&_surfDispMinus);

  KSPDestroy(&_kspMinus);


  PetscViewerDestroy(&_surfDispMinusViewer);

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::~FullLinearElastic in lithosphere.cpp.\n");
#endif
}


//===================== private member functions =======================


PetscErrorCode FullLinearElastic::computeShearStress()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::computeShearStress in lithosphere.cpp.\n");CHKERRQ(ierr);
#endif

  ierr = MatMult(_sbpPlus._muxDy_Iz,_uPlus,_sigma_xyPlus);CHKERRQ(ierr);
  ierr = MatMult(_sbpMinus._muxDy_Iz,_uMinus,_sigma_xyMinus);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::computeShearStress in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


/* Set displacement at sides equal to steady-sliding values:
 *   u ~ tau_fric*L/mu
 */
PetscErrorCode FullLinearElastic::setShifts()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::setShifts in lithosphere.cpp\n");CHKERRQ(ierr);
#endif


  PetscInt Ii,Istart,Iend;
  PetscScalar v,bcRshift;
  ierr = VecGetOwnershipRange(_bcRPlusShift,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    v = _fault.getTauInf(Ii);
    //~v = 0;
    v = 0.8*v;
    bcRshift = v*_Ly/_muArrPlus[_Ny*_Nz-_Nz+Ii]; // use last values of muArr

    ierr = VecSetValue(_bcRPlusShift,Ii,bcRshift,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_bcRPlusShift);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcRPlusShift);CHKERRQ(ierr);


  ierr = VecGetOwnershipRange(_bcLMinusShift,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    v = _fault.getTauInf(Ii);
     //~v = 0;
     v = 0.8*v;
    bcRshift = -v*_Ly/_muArrMinus[_Ny*_Nz-_Nz+Ii]; // use last values of muArr
    ierr = VecSetValue(_bcLMinusShift,Ii,bcRshift,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_bcLMinusShift);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_bcLMinusShift);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::setShifts in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode FullLinearElastic::setSurfDisp()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::setSurfDisp in lithosphere.cpp\n");CHKERRQ(ierr);
#endif

  PetscInt    Ii,Istart,Iend;
  PetscScalar u,y,z;
  ierr = VecGetOwnershipRange(_uPlus,&Istart,&Iend);
  for (Ii=Istart;Ii<Iend;Ii++) {
    z = Ii-_Nz*(Ii/_Nz);
    y = Ii/_Nz;
    if (z == 0) {
      ierr = VecGetValues(_uPlus,1,&Ii,&u);CHKERRQ(ierr);
      ierr = VecSetValue(_surfDispPlus,y,u,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_surfDispPlus);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_surfDispPlus);CHKERRQ(ierr);


  ierr = VecGetOwnershipRange(_uMinus,&Istart,&Iend);
  for (Ii=Istart;Ii<Iend;Ii++) {
    z = Ii-_Nz*(Ii/_Nz);
    y = Ii/_Nz;
    if (z == 0) {
      ierr = VecGetValues(_uMinus,1,&Ii,&u);CHKERRQ(ierr);
      ierr = VecSetValue(_surfDispMinus,y,u,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_surfDispMinus);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_surfDispMinus);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::setSurfDisp in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode FullLinearElastic::writeStep()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::writeStep in lithosphere.cpp at step %i\n",_stepCount);CHKERRQ(ierr);
#endif
  double startTime = MPI_Wtime();

  if (_stepCount==0) {
    ierr = _sbpPlus.writeOps(_outputDir+"plus_");CHKERRQ(ierr);
    if (_problemType.compare("full")==0) { ierr = _sbpMinus.writeOps(_outputDir+"minus_");CHKERRQ(ierr); }
    ierr = _fault.writeContext(_outputDir);CHKERRQ(ierr);
    ierr = PetscViewerASCIIOpen(PETSC_COMM_WORLD,(_outputDir+"time.txt").c_str(),&_timeViewer);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispPlus").c_str(),FILE_MODE_WRITE,
                                 &_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_surfDispPlus,_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispPlus").c_str(),
                                   FILE_MODE_APPEND,&_surfDispPlusViewer);CHKERRQ(ierr);

    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispMinus").c_str(),FILE_MODE_WRITE,
                                 &_surfDispMinusViewer);CHKERRQ(ierr);
    ierr = VecView(_surfDispMinus,_surfDispMinusViewer);CHKERRQ(ierr);
    ierr = PetscViewerDestroy(&_surfDispMinusViewer);CHKERRQ(ierr);
    ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(_outputDir+"surfDispMinus").c_str(),
                                   FILE_MODE_APPEND,&_surfDispMinusViewer);CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_surfDispPlus,_surfDispPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_surfDispMinus,_surfDispMinusViewer);CHKERRQ(ierr);
  }
  ierr = _fault.writeStep(_outputDir,_stepCount);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(_timeViewer, "%.15e\n",_currTime);CHKERRQ(ierr);

  _writeTime += MPI_Wtime() - startTime;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::writeStep in lithosphere.cpp at step %i\n",_stepCount);CHKERRQ(ierr);
#endif
  return ierr;
}

PetscErrorCode FullLinearElastic::integrate()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting LinearElastic::integrate in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  double startTime = MPI_Wtime();

  // call odeSolver routine integrate here
  _quadrature->setTolerance(_atol);CHKERRQ(ierr);
  _quadrature->setTimeStepBounds(_minDeltaT,_maxDeltaT);CHKERRQ(ierr);
  ierr = _quadrature->setTimeRange(_initTime,_maxTime);
  ierr = _quadrature->setInitialConds(_fault._var);CHKERRQ(ierr);

  ierr = _quadrature->integrate(this);CHKERRQ(ierr);
  _integrateTime += MPI_Wtime() - startTime;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending LinearElastic::integrate in lithosphere.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}

PetscErrorCode FullLinearElastic::setU()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::setU in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif


  PetscInt       Ii,Istart,Iend;
  PetscScalar    bcLPlus,bcRPlus,bcLMinus,bcRMinus,v;

  ierr = VecGetOwnershipRange(_bcRPlus,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(_bcLPlus,1,&Istart,&bcLPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRPlus,1,&Istart,&bcRPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcLMinus,1,&Istart,&bcLMinus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRMinus,1,&Istart,&bcRMinus);CHKERRQ(ierr);


  for (Ii=Istart;Ii<Iend;Ii++) {
    v = bcLPlus + Ii*(bcRPlus-bcLPlus)/_Ny;//bcL:(bcR-bcL)/(dom.Ny-1):bcR
    ierr = VecSetValues(_uPlus,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);

    v = bcLMinus + Ii*(bcRMinus - bcLMinus)/_Ny;
    ierr = VecSetValues(_uMinus,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_uPlus);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_uMinus);CHKERRQ(ierr);

  ierr = VecAssemblyEnd(_uPlus);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_uMinus);CHKERRQ(ierr);


  //~PetscPrintf(PETSC_COMM_WORLD,"_uPlus = \n");
  //~printVec(_uPlus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_uMinus = \n");
  //~printVec(_uMinus);
  //~assert(0>1);


#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::setU in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif
return ierr;
}


PetscErrorCode FullLinearElastic::setSigmaxy()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::setSigmaxy in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif


  PetscInt       Istart,Iend;
  PetscScalar    bcLPlus,bcRPlus,bcLMinus,bcRMinus;

  ierr = VecGetOwnershipRange(_bcRPlus,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(_bcLPlus,1,&Istart,&bcLPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRPlus,1,&Istart,&bcRPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcLMinus,1,&Istart,&bcLMinus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRMinus,1,&Istart,&bcRMinus);CHKERRQ(ierr);

  ierr = VecSet(_sigma_xyMinus,_muArrMinus[0]*(bcRMinus - bcLMinus)/_Ly);CHKERRQ(ierr);
  ierr = VecSet(_sigma_xyPlus,_muArrPlus[0]*(bcRPlus - bcLPlus)/_Ly);CHKERRQ(ierr);

  assert(_muArrPlus[0]*(bcRPlus - bcLPlus)/_Ly < 40);


#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::setSigmaxy in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif
return ierr;
}

PetscErrorCode FullLinearElastic::d_dt(const PetscScalar time,const_it_vec varBegin,const_it_vec varEnd,
                 it_vec dvarBegin,it_vec dvarEnd)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullLinearElastic::d_dt in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif

  // update boundaries: + side
  ierr = VecCopy(*(varBegin+1),_bcLPlus);CHKERRQ(ierr);
  ierr = VecSet(_bcRPlus,_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcRPlus,1.0,_bcRPlusShift);CHKERRQ(ierr);

  // update boundaries: - side
  ierr = VecCopy(*(varBegin+2),_bcRMinus);CHKERRQ(ierr);
  ierr = VecSet(_bcLMinus,-_vL*time/2.0);CHKERRQ(ierr);
  ierr = VecAXPY(_bcLMinus,1.0,_bcLMinusShift);CHKERRQ(ierr);

   // solve for displacement: + side
  ierr = _sbpPlus.setRhs(_rhsPlus,_bcLPlus,_bcRPlus,_bcTPlus,_bcBPlus);CHKERRQ(ierr);
  double startTime = MPI_Wtime();
  ierr = KSPSolve(_kspPlus,_rhsPlus,_uPlus);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;


  // solve for displacement: - side
  ierr = _sbpMinus.setRhs(_rhsMinus,_bcLMinus,_bcRMinus,_bcTMinus,_bcBMinus);CHKERRQ(ierr);
  startTime = MPI_Wtime();
  ierr = KSPSolve(_kspMinus,_rhsMinus,_uMinus);CHKERRQ(ierr);
  _linSolveTime += MPI_Wtime() - startTime;
  _linSolveCount++;

  // set _uPlus, _uMinus analytically
  //~setU();


  // solve for shear stress
  ierr = MatMult(_sbpMinus._muxDy_Iz,_uMinus,_sigma_xyMinus);CHKERRQ(ierr);
  ierr = MatMult(_sbpPlus._muxDy_Iz,_uPlus,_sigma_xyPlus);CHKERRQ(ierr);



  // set shear stresses analytically
  //~setSigmaxy();

  //~PetscPrintf(PETSC_COMM_WORLD,"_sigma_xyMinus = \n");
  //~printVec(_sigma_xyMinus);
  //~PetscPrintf(PETSC_COMM_WORLD,"_sigma_xyPlus = \n");
  //~printVec(_sigma_xyPlus);
  //~assert(0>1);

  ierr = _fault.setTauQS(_sigma_xyPlus,_sigma_xyMinus);CHKERRQ(ierr);
  ierr = _fault.d_dt(varBegin,varEnd, dvarBegin, dvarEnd);

  ierr = setSurfDisp();

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullLinearElastic::d_dt in lithosphere.cpp: time=%.15e\n",time);CHKERRQ(ierr);
#endif
  return ierr;
}



// Outputs data at each time step.
PetscErrorCode FullLinearElastic::debug(const PetscReal time,const PetscInt stepCount,
                     const_it_vec varBegin,const_it_vec varEnd,
                     const_it_vec dvarBegin,const_it_vec dvarEnd,const char *stage)
{
  PetscErrorCode ierr = 0;
#if ODEPRINT > 0
  PetscInt       Istart,Iend;
  PetscScalar    bcRPlus,bcLMinus,uMinus,uPlus,psi,velMinus,velPlus,dPsi,
                 tauQSPlus,tauQSMinus;

  //~PetscScalar k = _muArrPlus[0]/2/_Ly;

  ierr= VecGetOwnershipRange(*varBegin,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(*varBegin,1,&Istart,&psi);CHKERRQ(ierr);

  ierr = VecGetValues(*(varBegin+1),1,&Istart,&uPlus);CHKERRQ(ierr);
  ierr = VecGetValues(*(varBegin+2),1,&Istart,&uMinus);CHKERRQ(ierr);

  ierr= VecGetOwnershipRange(*dvarBegin,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(*dvarBegin,1,&Istart,&dPsi);CHKERRQ(ierr);
  ierr = VecGetValues(*(dvarBegin+1),1,&Istart,&velPlus);CHKERRQ(ierr);
  ierr = VecGetValues(*(dvarBegin+2),1,&Istart,&velMinus);CHKERRQ(ierr);

  ierr= VecGetOwnershipRange(_bcRPlus,&Istart,&Iend);CHKERRQ(ierr);
  ierr = VecGetValues(_bcRPlus,1,&Istart,&bcRPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_bcLMinus,1,&Istart,&bcLMinus);CHKERRQ(ierr);

  ierr = VecGetValues(_fault._tauQSPlus,1,&Istart,&tauQSPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_fault._tauQSMinus,1,&Istart,&tauQSMinus);CHKERRQ(ierr);

  if (stepCount == 0) {
    //~ierr = PetscPrintf(PETSC_COMM_WORLD,"%-4s|| %-4s %-6s | %-15s %-15s %-15s | %-15s %-15s %-16s | %-15s\n",
                       //~"Side","Step","Stage","gR","D","Q","VL","V","dQ","time");
    //~CHKERRQ(ierr);
  }
  ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %-6s | %.9e %.9e | %.9e %.9e | %.9e\n",stepCount,stage,
              uPlus-uMinus,psi,velPlus-velMinus,dPsi,time);CHKERRQ(ierr);

#if ODEPRINT > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"    y>0 |  %.9e  %.9e %.9e  %.9e \n",
              bcRPlus,uPlus,tauQSPlus,velPlus);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"    y<0 | %.9e %.9e %.9e %.9e \n",
              bcLMinus,uMinus,tauQSMinus,velMinus);CHKERRQ(ierr);
#endif
#endif
  return ierr;
}
