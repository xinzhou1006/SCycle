#include <petscts.h>
#include <petscviewerhdf5.h>
//~#include <iostream>
#include <sstream>
#include <string>
#include "userContext.h"

// constructor
UserContext::UserContext(const PetscInt ord,const PetscInt y,const  PetscInt z,const std::string outFile)
:outFileRoot(outFile),
 order(ord),Ny(y),Nz(z),N(y*z),
 Ly(0),Lz(0),H(0),dy(0),dz(0),
 //~cs(0),G(0),rho(0),f0(0),v0(0),vp(0),D_c(0),
 cs(0),f0(0),v0(0),vp(0),D_c(0),
 muIn(0),muOut(0),D(0),W(0),rhoIn(0),rhoOut(0),
 kspTol(0),rootTol(0),rootIts(0),
 strideLength(1),maxStepCount(1),initTime(0),currTime(0),maxTime(0),
 minDeltaT(1e-14),maxDeltaT(1e-14),
 count(0),atol(1e-14),initDeltaT(1e-14),
 computeTauTime(0),computeVelTime(0),kspTime(0),computeRhsTime(0),agingLawTime(0),rhsTime(0),
 fullLinOps(0),arrLinOps(0)
{

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting constructor in userContext.cpp.\n");
#endif

  std::ostringstream convert;
  convert << "./matlabAnswers/order" << order << "Ny" << y << "Nz" << z << "/";
  debugFolder = convert.str();

  //~PetscViewerHDF5Open(PETSC_COMM_WORLD, "data.h5", FILE_MODE_WRITE, &viewer);
  //~PetscViewerHDF5PushGroup(viewer, "/");
  //~PetscViewerHDF5PushGroup(viewer, "/timeSeries");
  //~PetscViewerHDF5SetTimestep(viewer, 0);


  // Initialize elastic coefficients and frictional parameters
  VecCreate(PETSC_COMM_WORLD,&eta);
  VecSetSizes(eta,PETSC_DECIDE,Nz);
  VecSetFromOptions(eta);     PetscObjectSetName((PetscObject) eta, "eta");
  VecDuplicate(eta,&sigma_N); PetscObjectSetName((PetscObject) sigma_N, "sigma_N");
  VecDuplicate(eta,&a);       PetscObjectSetName((PetscObject) a, "frica");
  VecDuplicate(eta,&b);       PetscObjectSetName((PetscObject) b, "fricb");
  VecDuplicate(eta,&psi);     PetscObjectSetName((PetscObject) psi, "psi");
  VecDuplicate(eta,&tempPsi);
  VecDuplicate(eta,&tau);     PetscObjectSetName((PetscObject) tau, "tau_qs");
  VecDuplicate(eta,&gRShift);


  muArr = new PetscScalar[Ny*Nz];

  // Initialize boundary conditions
  VecDuplicate(eta,&gF);
  VecDuplicate(gF,&gR);
  VecCreate(PETSC_COMM_WORLD,&gS);
  VecSetSizes(gS,PETSC_DECIDE,Ny);
  VecSetFromOptions(gS);
  VecDuplicate(gS,&gD);

  // Initialize the linear system

  MatCreate(PETSC_COMM_WORLD,&A);     PetscObjectSetName((PetscObject) A, "matA");
  MatCreate(PETSC_COMM_WORLD,&D2y);
  MatCreate(PETSC_COMM_WORLD,&D2z);
  MatCreate(PETSC_COMM_WORLD,&Dy_Iz); PetscObjectSetName((PetscObject) Dy_Iz, "Dy_Iz");

  MatCreate(PETSC_COMM_WORLD,&Hinvy);
  MatCreate(PETSC_COMM_WORLD,&Hinvz);

  MatCreate(PETSC_COMM_WORLD,&Hinvy_Izxe0y_Iz);  PetscObjectSetName((PetscObject) Hinvy_Izxe0y_Iz, "Hinvy_Izxe0y_Iz");
  MatCreate(PETSC_COMM_WORLD,&Hinvy_IzxeNy_Iz);  PetscObjectSetName((PetscObject) Hinvy_IzxeNy_Iz, "Hinvy_IzxeNy_Iz");
  MatCreate(PETSC_COMM_WORLD,&Iy_HinvzxIy_e0z);  PetscObjectSetName((PetscObject) Iy_HinvzxIy_e0z, "Iy_HinvzxIy_e0z");
  MatCreate(PETSC_COMM_WORLD,&Iy_HinvzxIy_eNz);  PetscObjectSetName((PetscObject) Iy_HinvzxIy_eNz, "Iy_HinvzxIy_eNz");
  MatCreate(PETSC_COMM_WORLD,&Hinvy_IzxBySy_IzTxe0y_Iz);
    PetscObjectSetName((PetscObject) Hinvy_IzxBySy_IzTxe0y_Iz, "Hinvy_IzxBySy_IzTxe0y_Iz");
  MatCreate(PETSC_COMM_WORLD,&Hinvy_IzxBySy_IzTxeNy_Iz);
    PetscObjectSetName((PetscObject) Hinvy_IzxBySy_IzTxeNy_Iz, "Hinvy_IzxBySy_IzTxeNy_Iz");

  MatCreate(PETSC_COMM_WORLD,&mu);     PetscObjectSetName((PetscObject) mu, "mu");

  VecCreate(PETSC_COMM_WORLD,&rhs);
  VecSetSizes(rhs,PETSC_DECIDE,Ny*Nz);
  VecSetFromOptions(rhs);
  VecDuplicate(rhs,&uhat);

  KSPCreate(PETSC_COMM_WORLD,&ksp);

  // initialize time stepping fields
  VecDuplicate(eta,&vel);                  PetscObjectSetName((PetscObject) vel, "vel");
  VecDuplicate(eta,&faultDisp);            PetscObjectSetName((PetscObject) faultDisp, "faultDisp");
  VecCreate(PETSC_COMM_WORLD,&surfDisp);
  VecSetSizes(surfDisp,PETSC_DECIDE,Ny);
  VecSetFromOptions(surfDisp);             PetscObjectSetName((PetscObject) surfDisp, "surfDisp");
  VecDuplicate(eta,&dpsi);
  var = new Vec[2];
  var[0] = faultDisp; var[1] = psi;

  // viewers (bc PetSc appears to have an error in the way it handles them)
  PetscViewerASCIIOpen(PETSC_COMM_WORLD,(outFileRoot+"time.txt").c_str(),&timeViewer);
  PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"surfDisp").c_str(),FILE_MODE_WRITE,&surfDispViewer);
  PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"faultDisp").c_str(),FILE_MODE_WRITE,&faultDispViewer);
  PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"vel").c_str(),FILE_MODE_WRITE,&velViewer);
  PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"tau").c_str(),FILE_MODE_WRITE,&tauViewer);
  PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"psi").c_str(),FILE_MODE_WRITE,&psiViewer);

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending constructor in userContext.cpp.\n");
#endif


}

// destructor
UserContext::~UserContext()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting destructor in userContext.cpp.\n");
#endif

  // Elastic Coefficients and Frictional Parameters
  VecDestroy(&eta);
  VecDestroy(&sigma_N);
  VecDestroy(&a);
  VecDestroy(&b);
  VecDestroy(&psi);
  VecDestroy(&tau);
  VecDestroy(&gRShift);

  // boundary conditions
  VecDestroy(&gF);
  VecDestroy(&gS);
  VecDestroy(&gR);
  VecDestroy(&gD);

  // linear system
  VecDestroy(&rhs);

  MatDestroy(&A);
  MatDestroy(&D2y);
  MatDestroy(&D2z);
  MatDestroy(&Dy_Iz);
  MatDestroy(&Hinvy);
  MatDestroy(&Hinvz);

  MatDestroy(&Hinvy_Izxe0y_Iz);
  MatDestroy(&Hinvy_IzxeNy_Iz);
  MatDestroy(&Iy_HinvzxIy_e0z);
  MatDestroy(&Iy_HinvzxIy_eNz);
  MatDestroy(&Hinvy_IzxBySy_IzTxe0y_Iz);
  MatDestroy(&Hinvy_IzxBySy_IzTxeNy_Iz);

  MatDestroy(&mu);
  delete[] muArr;

  KSPDestroy(&ksp);
  VecDestroy(&rhs);
  VecDestroy(&uhat);

  // time stepping system
  VecDestroy(&faultDisp);
  VecDestroy(&surfDisp);
  VecDestroy(&vel);
  delete[] var;

  VecDestroy(&tempPsi);

  // viewers
  //~PetscViewerDestroy(&viewer);
  PetscViewerDestroy(&timeViewer);
  PetscViewerDestroy(&faultDispViewer);
  PetscViewerDestroy(&surfDispViewer);
  PetscViewerDestroy(&velViewer);
  PetscViewerDestroy(&tauViewer);
  PetscViewerDestroy(&psiViewer);

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending destructor in userContext.cpp.\n");
#endif
}

PetscErrorCode UserContext::writeParameters()
{

  PetscErrorCode ierr;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting writeParameters in userContext.c\n");CHKERRQ(ierr);
#endif

  std::string str = outFileRoot + "parameters.txt";
  const char * outFileLoc = str.c_str();
  PetscViewer    outviewer;

  PetscViewerCreate(PETSC_COMM_WORLD, &outviewer);
  PetscViewerSetType(outviewer, PETSCVIEWERASCII);
  PetscViewerFileSetMode(outviewer, FILE_MODE_WRITE);
  PetscViewerFileSetName(outviewer, outFileLoc);

  ierr = PetscViewerASCIIPrintf(outviewer,"order = %i\n",order);CHKERRQ(ierr);

  // domain geometry
  ierr = PetscViewerASCIIPrintf(outviewer,"Ny = %i\n",Ny);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"Nz = %i\n",Nz);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"Ly = %f\n",Ly);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"Lz = %f\n",Lz);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"H = %f\n",H);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"dy = %f\n",dy);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"dz = %f\n",dz);CHKERRQ(ierr);

  //  frictional parameters
  ierr = PetscViewerASCIIPrintf(outviewer,"f0 = %f\n",f0);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"v0 = %f\n",v0);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"vp = %f\n",vp);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"D_c = %f\n",D_c);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"muIn = %f\n",muIn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"muOut = %f\n",muOut);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"rhoIn = %f\n",rhoIn);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"rhoOut = %f\n",rhoOut);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"D = %f\n",D);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"W = %f\n",W);CHKERRQ(ierr);

  // tolerances for linear and nonlinear (for vel) solve
  ierr = PetscViewerASCIIPrintf(outviewer,"kspTol = %f\n",kspTol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"rootTol = %f\n",rootTol);CHKERRQ(ierr);

  //  constitutive parameters
  //~ierr = PetscViewerASCIIPrintf(outviewer,"cs = %f\n",cs);CHKERRQ(ierr);
  //~ierr = PetscViewerASCIIPrintf(outviewer,"G = %f\n",G);CHKERRQ(ierr);
  //~ierr = PetscViewerASCIIPrintf(outviewer,"rho = %f\n",rho);CHKERRQ(ierr);

  // time monitering
  ierr = PetscViewerASCIIPrintf(outviewer,"strideLength = %i\n",strideLength);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"maxStepCount = %i\n",maxStepCount);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"initTime = %f\n",initTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"maxTime = %f\n",maxTime);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"atol = %f\n",atol);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"initDeltaT = %f\n",initDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"minDeltaT = %f\n",minDeltaT);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(outviewer,"maxDeltaT = %f\n",maxDeltaT);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending writeParameters in userContext.c\n");CHKERRQ(ierr);
#endif

  return 0;
}

PetscErrorCode UserContext::writeOperators()
{
  PetscErrorCode ierr;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting writeOperators in userContext.c\n");CHKERRQ(ierr);
#endif

  PetscViewer    outviewer;
  const char     *outFileLoc;
//~
  std::string str =  "A";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(A,outviewer);CHKERRQ(ierr);

  str = "Dy_Iz";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Dy_Iz,outviewer);CHKERRQ(ierr);

  str = "Hinvy_Izxe0y_Iz";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Hinvy_Izxe0y_Iz,outviewer);CHKERRQ(ierr);

  str = "Hinvy_IzxBySy_IzTxe0y_Iz";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Hinvy_IzxBySy_IzTxe0y_Iz,outviewer);CHKERRQ(ierr);

  str = "Hinvy_IzxeNy_Iz";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Hinvy_IzxeNy_Iz,outviewer);CHKERRQ(ierr);

  str = "Hinvy_IzxBySy_IzTxeNy_Iz";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Hinvy_IzxBySy_IzTxeNy_Iz,outviewer);CHKERRQ(ierr);

  str = "Iy_HinvzxIy_e0z";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Iy_HinvzxIy_e0z,outviewer);CHKERRQ(ierr);

  str = "Iy_HinvzxIy_eNz";
  outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&outviewer);CHKERRQ(ierr);
  ierr = MatView(Iy_HinvzxIy_eNz,outviewer);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending writeParameters in userContext.c\n");CHKERRQ(ierr);
#endif

  return ierr;
};

PetscErrorCode UserContext::writeInitialStep()
{
  PetscErrorCode ierr;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting writeInitialStep in userContext.c\n");CHKERRQ(ierr);
#endif

  double startTime = MPI_Wtime();

  PetscViewerASCIIPrintf(timeViewer, "%f\n", currTime);
  ierr = VecView(faultDisp,faultDispViewer);CHKERRQ(ierr);
  ierr = VecView(surfDisp,surfDispViewer);CHKERRQ(ierr);
  ierr = VecView(vel,velViewer);CHKERRQ(ierr);
  ierr = VecView(tau,tauViewer);CHKERRQ(ierr);
  ierr = VecView(psi,psiViewer);CHKERRQ(ierr);

  ierr = PetscViewerDestroy(&faultDispViewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&surfDispViewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&velViewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&tauViewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&psiViewer);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"faultDisp").c_str(),
                               FILE_MODE_APPEND,&faultDispViewer);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"surfDisp").c_str(),
                               FILE_MODE_APPEND,&surfDispViewer);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"vel").c_str(),
                               FILE_MODE_APPEND,&velViewer);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"tau").c_str(),
                               FILE_MODE_APPEND,&tauViewer);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outFileRoot+"psi").c_str(),
                               FILE_MODE_APPEND,&psiViewer);CHKERRQ(ierr);

  writeTime += MPI_Wtime() - startTime;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending writeParameters in userContext.c\n");CHKERRQ(ierr);
#endif

  return ierr;
}


PetscErrorCode UserContext::writeStep()
{
  PetscErrorCode ierr;

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting writeCurrentStep in userContext.cpp.\n");
#endif

  double startTime = MPI_Wtime();

  //~ierr = PetscViewerHDF5PushGroup(viewer, "/timeSeries");CHKERRQ(ierr);
//~
  //~ierr = VecView(vel, viewer);CHKERRQ(ierr);
  //~ierr = VecView(psi, viewer);CHKERRQ(ierr);
  //~ierr = VecView(tau, viewer);CHKERRQ(ierr);
  //~ierr = VecView(surfDisp, viewer);CHKERRQ(ierr);
  //~ierr = VecView(faultDisp, viewer);CHKERRQ(ierr);
//~
  //~ierr = PetscViewerHDF5PopGroup(viewer);CHKERRQ(ierr);


  PetscViewerASCIIPrintf(timeViewer, "%f\n", currTime);

  PetscViewerASCIIPrintf(timeViewer, "%f\n", currTime);
  ierr = VecView(faultDisp,faultDispViewer);CHKERRQ(ierr);
  ierr = VecView(surfDisp,surfDispViewer);CHKERRQ(ierr);
  ierr = VecView(vel,velViewer);CHKERRQ(ierr);
  ierr = VecView(tau,tauViewer);CHKERRQ(ierr);
  ierr = VecView(psi,psiViewer);CHKERRQ(ierr);


  writeTime += MPI_Wtime() - startTime;

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending writeCurrentStep in userContext.cpp.\n");
#endif

  return 0;
}


PetscErrorCode UserContext::writeRateAndState()
{
  PetscErrorCode ierr = 0;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting writeRateAndState in rateAndState.c\n");CHKERRQ(ierr);
#endif


  //~ierr = PetscViewerHDF5PushGroup(viewer, "/frictionContext");CHKERRQ(ierr);
//~
  //~ierr = VecView(a, viewer);CHKERRQ(ierr);
  //~ierr = VecView(b, viewer);CHKERRQ(ierr);
  //~ierr = VecView(eta, viewer);CHKERRQ(ierr);
  //~ierr = VecView(sigma_N, viewer);CHKERRQ(ierr);
  //~ierr = MatView(mu, viewer);CHKERRQ(ierr);
//~
//~
  //~ierr = PetscViewerHDF5PopGroup(viewer);CHKERRQ(ierr);

  PetscViewer    viewer;
  const char * outFileLoc;

  std::string str = D.outFileRoot + "a"; outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(D.a,viewer);CHKERRQ(ierr);

  str = D.outFileRoot + "b"; outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(D.b,viewer);CHKERRQ(ierr);

  str = D.outFileRoot + "eta"; outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(D.eta,viewer);CHKERRQ(ierr);

  str = D.outFileRoot + "sigma_N"; outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(D.sigma_N,viewer);CHKERRQ(ierr);

  str = D.outFileRoot + "mu"; outFileLoc = str.c_str();
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,outFileLoc,FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = MatView(D.mu,viewer);CHKERRQ(ierr);

  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending writeRateAndState in rateAndState.c\n");CHKERRQ(ierr);
#endif

  return ierr;
}


PetscErrorCode UserContext::printTiming()
{
  PetscErrorCode ierr = 0;

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting printTiming in userContext.cpp.\n");
#endif

  ierr = PetscPrintf(PETSC_COMM_WORLD,"fullLinOps = %g\n",fullLinOps);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"arrLinOps = %g\n",arrLinOps);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"computeTauTime = %g\n",computeTauTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"computeVelTime = %g\n",computeVelTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"kspTime = %g\n",kspTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"computeRhsTime = %g\n",computeRhsTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"agingLawTime = %g\n",agingLawTime);CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,"rhsTime = %g\n",rhsTime);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"rootIts = %i\n",rootIts);CHKERRQ(ierr);

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending printTiming in userContext.cpp.\n");
#endif


  return ierr;
}


PetscErrorCode UserContext::loadCurrentPlace()
{
  PetscErrorCode ierr = 0;
  PetscViewer    fd;

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"dataMatlab/psi",FILE_MODE_READ,&fd);CHKERRQ(ierr);
  //~ierr = MatCreate(PETSC_COMM_WORLD,&D->A);CHKERRQ(ierr);
  //~ierr = MatSetType(D->A,matType);CHKERRQ(ierr);
  ierr = VecLoad(psi,fd);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&fd);CHKERRQ(ierr);

  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"dataMatlab/faultDisp",FILE_MODE_READ,&fd);CHKERRQ(ierr);
  //~ierr = MatCreate(PETSC_COMM_WORLD,&D->A);CHKERRQ(ierr);
  //~ierr = MatSetType(D->A,matType);CHKERRQ(ierr);
  ierr = VecLoad(faultDisp,fd);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&fd);CHKERRQ(ierr);

  currTime=4.376188933923020e+05;



  return ierr;
}
