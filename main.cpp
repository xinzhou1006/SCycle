#include <petscts.h>
#include <petscviewerhdf5.h>
#include <string>

#include "domain.hpp"
//~#include "odeSolver.hpp"
#include "lithosphere.hpp"
#include "sbpOps.hpp"
#include "spmat.hpp"



using namespace std;




int runTests(const char * inputFile)
{
  PetscErrorCode ierr = 0;

  //~spmatTests();
  Domain domain(inputFile);
  //~Domain domain(inputFile,5,4);
  domain.write();
  //~SbpOps sbp(domain);

  //~Fault fault(domain);
  //~fault.writeContext(domain._outputDir);
  //~fault.writeStep(domain._outputDir,0);

  Lithosphere lith(domain);
  ierr = lith.writeStep();CHKERRQ(ierr);
  ierr = lith.view();CHKERRQ(ierr);

  return ierr;
}

/* This has not been rewritten since the post-quals refactoring
int screwDislocation(PetscInt Ny,PetscInt Nz)
{
  PetscErrorCode ierr = 0;
  PetscInt       order=4;
  PetscBool      loadMat = PETSC_FALSE;
  PetscViewer    viewer;
  PetscScalar    u,z;


  // set up the problem context
  UserContext D(order,Ny,Nz,"data/");
  ierr = setParameters(D);CHKERRQ(ierr);
  ierr = D.writeParameters();CHKERRQ(ierr);
  ierr = setRateAndState(D);CHKERRQ(ierr);
  ierr = D.writeRateAndState();CHKERRQ(ierr);
  ierr = setLinearSystem(D,loadMat);CHKERRQ(ierr);
  ierr = D.writeOperators();CHKERRQ(ierr);

  // set boundary conditions
  ierr = VecSet(D.gS,0.0);CHKERRQ(ierr); // surface
  ierr = VecSet(D.gD,0.0);CHKERRQ(ierr); // depth
  ierr = VecSet(D.gR,0.5);CHKERRQ(ierr); // remote

  // fault
  PetscInt Ii,Istart,Iend, N1 = D.H/D.dz;
  ierr = VecGetOwnershipRange(D.gF,&Istart,&Iend);
  for (Ii=Istart;Ii<Iend;Ii++) {
    if (Ii<N1) { ierr = VecSetValue(D.gF,Ii,0.0,INSERT_VALUES); }
    else { ierr = VecSetValue(D.gF,Ii,0.5,INSERT_VALUES); }
  }
  ierr = VecAssemblyBegin(D.gF);CHKERRQ(ierr);  ierr = VecAssemblyEnd(D.gF);CHKERRQ(ierr);

  // compute analytic surface displacement
  Vec anal;
  ierr = VecDuplicate(D.surfDisp,&anal);CHKERRQ(ierr);

  ierr = VecGetOwnershipRange(anal,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    z = Ii-D.Nz*(Ii/D.Nz);
    //~y = D.dy*(Ii/D.Nz);
    u = (1.0/PETSC_PI)*atan(D.dy*Ii/D.H);
    ierr = VecSetValues(anal,1,&Ii,&u,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(anal);CHKERRQ(ierr);  ierr = VecAssemblyEnd(anal);CHKERRQ(ierr);

  //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"data/anal",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  //~ierr = VecView(anal,viewer);CHKERRQ(ierr);
  //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"data/gR",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  //~ierr = VecView(D.gR,viewer);CHKERRQ(ierr);
  //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"data/gF",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  //~ierr = VecView(D.gF,viewer);CHKERRQ(ierr);

  ierr = ComputeRHS(D);CHKERRQ(ierr); // assumes gS and gD are 0
  ierr = KSPSolve(D.ksp,D.rhs,D.uhat);CHKERRQ(ierr);

  // pull out surface displacement
  PetscInt ind;
  ierr = VecGetOwnershipRange(D.uhat,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    z = Ii-D.Nz*(Ii/D.Nz);
    ind = Ii/D.Nz;
    if (z == 0) {
      ierr = VecGetValues(D.uhat,1,&Ii,&u);CHKERRQ(ierr);
      ierr = VecSetValues(D.surfDisp,1,&ind,&u,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(D.surfDisp);CHKERRQ(ierr);  ierr = VecAssemblyEnd(D.surfDisp);CHKERRQ(ierr);

  std::ostringstream fileName;
  fileName << "data/surfDisp" << order << "Ny" << Ny;
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,fileName.str().c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  //~ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,"data/surfDisp",FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(D.surfDisp,viewer);CHKERRQ(ierr);


  // compute error
  PetscScalar maxErr[2] = {0.0,0.0};
  Vec diff;
  VecDuplicate(D.surfDisp,&diff);CHKERRQ(ierr);

  ierr = VecWAXPY(diff,-1.0,D.surfDisp,anal);CHKERRQ(ierr);
  ierr = VecAbs(diff);CHKERRQ(ierr);
  ierr = VecNorm(diff,NORM_2,&maxErr[0]);CHKERRQ(ierr);
  ierr = VecNorm(diff,NORM_INFINITY,&maxErr[1]);CHKERRQ(ierr);

  ierr = PetscPrintf(PETSC_COMM_WORLD,"%i %i %g %g %g %g %.9g %.9g\n",
                     D.Ny,D.Nz,D.dy,D.dz,D.Ly,D.Lz,maxErr[0],maxErr[1]);CHKERRQ(ierr);


  return ierr;
}
*/


// Note that due to a memory problem in PETSc, looping over this many
// times will result in an error.
PetscErrorCode writeVec(Vec vec,const char * loc)
{
  PetscErrorCode ierr = 0;
  PetscViewer    viewer;
  PetscViewerBinaryOpen(PETSC_COMM_WORLD,loc,FILE_MODE_WRITE,&viewer);
  ierr = VecView(vec,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
  return ierr;
}

/* Perform MMS in space:
 *  In order to test only the interior stencils and operators
 * use y,z = [0,2*pi]. This will make all boundary vectors 0, and so will
 * not test operators which map BC vectors to rhs and A.
 *
 * To test that BC vectors are being mapped correctly, use y,z =[L0,L]
 * where neither L0 nor L are multiples of pi.
 */
int mmsSpace(const char* inputFile,PetscInt Ny,PetscInt Nz)
{
  PetscErrorCode ierr = 0;
  PetscInt       Ii = 0;
  PetscScalar    y,z,v=0;

  Domain domain(inputFile,Ny,Nz);

  // set vectors containing analytical distribution for displacement and source
  Vec uAnal,source;
  ierr = VecCreate(PETSC_COMM_WORLD,&uAnal);CHKERRQ(ierr);
  ierr = VecSetSizes(uAnal,PETSC_DECIDE,Ny*Nz);CHKERRQ(ierr);
  ierr = VecSetFromOptions(uAnal);CHKERRQ(ierr);
  ierr = VecDuplicate(uAnal,&source);CHKERRQ(ierr);

  PetscInt *inds;
  ierr = PetscMalloc(Ny*Nz*sizeof(PetscInt),&inds);CHKERRQ(ierr);

  PetscScalar *uAnalArr,*sourceArr;
  ierr = PetscMalloc(Ny*Nz*sizeof(PetscScalar),&uAnalArr);CHKERRQ(ierr);
  ierr = PetscMalloc(Ny*Nz*sizeof(PetscScalar),&sourceArr);CHKERRQ(ierr);

  // boundary conditions
  Vec bcF,bcR,bcS,bcD,rhs;
  VecCreate(PETSC_COMM_WORLD,&bcF);
  VecSetSizes(bcF,PETSC_DECIDE,Nz);
  VecSetFromOptions(bcF);
  VecDuplicate(bcF,&bcR);
  VecCreate(PETSC_COMM_WORLD,&bcS);
  VecSetSizes(bcS,PETSC_DECIDE,Ny);
  VecSetFromOptions(bcS);
  VecDuplicate(bcS,&bcD);

  // set values for boundaries, source, and analytic solution
  PetscInt indx = 0;
  for (Ii=0;Ii<Ny*Nz;Ii++)
  {
    y = domain._dy*(Ii/Nz);
    z = domain._dz*(Ii-Nz*(Ii/Nz));
    inds[Ii] = Ii;

    uAnalArr[Ii] = sin(y)*cos(z);
    sourceArr[Ii] = cos(y+z)*(-cos(y)*cos(z) + sin(y)*sin(z)) + 2*(sin(y+z)+2)*cos(z)*sin(y);

    // BCs
    if (y==0) {
      v = sin(y)*cos(z);
      ierr = VecSetValue(bcF,Ii,v,INSERT_VALUES);CHKERRQ(ierr);
    }
    if (y==domain._Ly) {
      indx = z/domain._dz;
      v = sin(y)*cos(z);
      ierr = VecSetValue(bcR,indx,v,INSERT_VALUES);CHKERRQ(ierr);
    }
    if (z==0) {
      indx = (int) (y/domain._dy);
      v = -domain._muArr[Ii]*sin(y)*sin(z);
      ierr = VecSetValue(bcS,indx,v,INSERT_VALUES);CHKERRQ(ierr);
    }
    if (z==domain._Lz) {
      indx = (int) (y/domain._dy);
      v = -domain._muArr[Ii]*sin(y)*sin(z);
      ierr = VecSetValue(bcD,indx,v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecSetValues(uAnal,Ny*Nz,inds,uAnalArr,INSERT_VALUES);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(uAnal);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(uAnal);CHKERRQ(ierr);

  ierr = VecSetValues(source,Ny*Nz,inds,sourceArr,INSERT_VALUES);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(source);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(source);CHKERRQ(ierr);

  VecAssemblyBegin(bcF); VecAssemblyEnd(bcF);
  VecAssemblyBegin(bcR); VecAssemblyEnd(bcR);
  VecAssemblyBegin(bcS); VecAssemblyEnd(bcS);
  VecAssemblyBegin(bcD); VecAssemblyEnd(bcD);

  //~ierr = VecView(bcF,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  //~ierr = VecView(bcR,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  //~ierr = VecView(bcS,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  //~ierr = VecView(bcD,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);


  // set up linear system
  SbpOps sbp(domain);



  VecCreate(PETSC_COMM_WORLD,&rhs);
  VecSetSizes(rhs,PETSC_DECIDE,Ny*Nz);
  VecSetFromOptions(rhs);
  VecSet(rhs,0.0);
  ierr = sbp.setRhs(rhs,bcF,bcR,bcS,bcD);CHKERRQ(ierr);
//~ierr = writeVec(rhs,"data/rhs_b");CHKERRQ(ierr);
 //~sbp.writeOps("data/");
//~assert(0>1);

  ierr = VecAXPY(rhs,-1.0,source);CHKERRQ(ierr); // rhs = rhs - source


  KSP ksp;
  PC  pc;
  KSPCreate(PETSC_COMM_WORLD,&ksp);
  ierr = KSPSetType(ksp,KSPPREONLY);CHKERRQ(ierr);
  ierr = KSPSetOperators(ksp,sbp._A,sbp._A,SAME_PRECONDITIONER);CHKERRQ(ierr);
  ierr = KSPGetPC(ksp,&pc);CHKERRQ(ierr);
  ierr = PCSetType(pc,PCLU);CHKERRQ(ierr);
  PCFactorSetMatSolverPackage(pc,MATSOLVERMUMPS);
  PCFactorSetUpMatSolverPackage(pc);
  ierr = KSPSetUp(ksp);CHKERRQ(ierr);
  ierr = KSPSetFromOptions(ksp);CHKERRQ(ierr);


  Vec uhat;
  ierr = VecDuplicate(rhs,&uhat);CHKERRQ(ierr);
  ierr = KSPSolve(ksp,rhs,uhat);CHKERRQ(ierr);


  // output vectors for visualization with matlab
  ierr = domain.write();
  ierr = writeVec(uhat,"data/uhat");CHKERRQ(ierr);
  ierr = writeVec(uAnal,"data/uAnal");CHKERRQ(ierr);
  ierr = writeVec(source,"data/source");CHKERRQ(ierr);
  ierr = writeVec(rhs,"data/rhs");CHKERRQ(ierr);

  ierr = writeVec(bcF,"data/bcF");CHKERRQ(ierr);
  ierr = writeVec(bcR,"data/bcR");CHKERRQ(ierr);
  ierr = writeVec(bcS,"data/bcS");CHKERRQ(ierr);
  ierr = writeVec(bcD,"data/bcD");CHKERRQ(ierr);
 //~sbp.writeOps("data/");


  // MMS for shear stress on fault
  Vec tauHat, tauAnal, sigma_xy;
  ierr = VecDuplicate(rhs,&sigma_xy);CHKERRQ(ierr);
  ierr = MatMult(sbp._Dy_Iz,uAnal,sigma_xy);CHKERRQ(ierr);

  ierr = VecDuplicate(bcF,&tauHat);CHKERRQ(ierr);
  ierr = VecDuplicate(bcF,&tauAnal);CHKERRQ(ierr);
  PetscInt Istart,Iend;
  v = 0.0;
  ierr = VecGetOwnershipRange(sigma_xy,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    if (Ii<Nz) {
      ierr = VecGetValues(sigma_xy,1,&Ii,&v);CHKERRQ(ierr);
      ierr = VecSetValues(tauHat,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);

      z = domain._dz*(Ii-Nz*(Ii/Nz));
      y = domain._dy*(Ii/Nz);
      v = domain._muArr[Ii]*cos(z)*cos(y);
      ierr = VecSetValues(tauAnal,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(tauHat);CHKERRQ(ierr); ierr = VecAssemblyBegin(tauAnal);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(tauHat);CHKERRQ(ierr); ierr = VecAssemblyEnd(tauAnal);CHKERRQ(ierr);
  //~ierr = VecView(tauHat,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);
  //~ierr = VecView(tauAnal,PETSC_VIEWER_STDOUT_WORLD);CHKERRQ(ierr);


  // measure error in L2 norm
  PetscScalar errU,errTau;
  ierr = VecAXPY(uAnal,-1.0,uhat);CHKERRQ(ierr); //overwrites 1st arg with sum
  ierr = VecNorm(uAnal,NORM_2,&errU);
  errU = errU/sqrt( (double) Ny*Nz );

  ierr = VecAXPY(tauAnal,-1.0,tauHat);CHKERRQ(ierr); //overwrites 1st arg with sum
  ierr = VecNorm(tauAnal,NORM_2,&errTau);
  errTau = errTau/sqrt( (double) Nz );


  ierr = PetscPrintf(PETSC_COMM_WORLD,"%5i %5i %5i %20.12e %20.12e\n",
                     domain._order,domain._Ny,domain._Nz,log2(errU),log2(errTau));CHKERRQ(ierr);


  // clean up
  VecDestroy(&uAnal);
  VecDestroy(&source);
  PetscFree(inds);
  PetscFree(uAnalArr);
  PetscFree(sourceArr);
  VecDestroy(&bcF);
  VecDestroy(&bcR);
  VecDestroy(&bcS);
  VecDestroy(&bcD);

  return ierr;
}


int runEqCycle(const char * inputFile)
{
  PetscErrorCode ierr = 0;

  Domain domain(inputFile);
  domain.write();
  Lithosphere lith(domain);

  ierr = lith.writeStep();CHKERRQ(ierr);
  ierr = lith.integrate();CHKERRQ(ierr);
  ierr = lith.view();CHKERRQ(ierr);
  return ierr;
}

int main(int argc,char **args)
{
  PetscInitialize(&argc,&args,NULL,NULL);

  PetscErrorCode ierr = 0;

  const char * inputFile;
  if (argc > 1) { inputFile = args[1]; }
  else { inputFile = "init.txt"; }

  /*
  PetscInt Ny = 401, Nz = 401;
  ierr = PetscOptionsGetInt(NULL,"-Ny",&Ny,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetInt(NULL,"-Nz",&Nz,NULL);CHKERRQ(ierr);
  screwDislocation(Ny,Nz);
  */

  //~ierr = sbpConvergence(argc,args);CHKERRQ(ierr);// perform MMS

  // compare screw dislocation with numerics
  /*
  PetscInt Ny=721, Nz=241;
  for (Nz=241;Nz<482;Nz+=120) {
    for (Ny=241;Ny<1442;Ny+=120) {
      screwDislocation(Ny,Nz);
    }
    ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);
  }
  ierr = PetscPrintf(PETSC_COMM_WORLD,"\n");CHKERRQ(ierr);
  */


  //~runEqCycle(inputFile);
  //~runTests(inputFile);


  //~PetscPrintf(PETSC_COMM_WORLD,"MMS:\n%5s %5s %5s %20s %20s\n",
             //~"order","Ny","Nz","log2(||u-u^||)","log2(||tau-tau^||)");
  //~PetscInt Ny=21;
  //~for (Ny=21;Ny<82;Ny=(Ny-1)*2+1)
  //~//for (Ny=21;Ny<322;Ny=(Ny-1)*2+1)
  //~{
    //~//PetscPrintf(PETSC_COMM_WORLD,"Ny=%i\n",Ny);
    //~mmsSpace(inputFile,Ny,Ny); // perform MMS
  //~}


  PetscFinalize();
  return ierr;
}

