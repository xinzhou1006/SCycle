#include "fault.hpp"

using namespace std;




Fault::Fault(Domain&D)
: _file(D._file),_delim(D._delim),
  _N(D._Nz),_sizeMuArr(D._Ny*D._Nz),_L(D._Lz),_h(_L/(_N-1.)),_Dc(D._Dc),
  _problemType(D._problemType),
  _depth(D._depth),_width(D._width),
  _rootTol(D._rootTol),_rootIts(0),_maxNumIts(1e8),
  _f0(D._f0),_v0(D._v0),_vL(D._vL),
  //~_aVals(D._aVals),_aDepths(D._aDepths),_bVals(D._bVals),_bDepths(D._bDepths),
  _a(NULL),_b(NULL),
  _psi(NULL),_tempPsi(NULL),_dPsi(NULL),
  //~_sigma_N_min(D._sigma_N_min),_sigma_N_max(D._sigma_N_max),_sigma_N(D._sigma_N),
  _sigma_N(NULL),
  _muArrPlus(D._muArrPlus),_csArrPlus(D._csArrPlus),_slip(NULL),_slipVel(NULL),
  _slipViewer(NULL),_slipVelViewer(NULL),_tauQSPlusViewer(NULL),
  _psiViewer(NULL),
  _tauQSP(NULL)
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting Fault::Fault in fault.cpp.\n");
#endif

// set viscosity
  loadSettings(_file);
  checkInput();

  // fields that exist on the fault
  VecCreate(PETSC_COMM_WORLD,&_tauQSP);
  VecSetSizes(_tauQSP,PETSC_DECIDE,_N);
  VecSetFromOptions(_tauQSP);     PetscObjectSetName((PetscObject) _tauQSP, "tau");
  VecDuplicate(_tauQSP,&_psi); PetscObjectSetName((PetscObject) _psi, "psi");
  VecDuplicate(_tauQSP,&_tempPsi); PetscObjectSetName((PetscObject) _tempPsi, "tempPsi");
  VecDuplicate(_tauQSP,&_dPsi); PetscObjectSetName((PetscObject) _dPsi, "dPsi");
  VecDuplicate(_tauQSP,&_slip); PetscObjectSetName((PetscObject) _slip, "_slip");
  VecDuplicate(_tauQSP,&_slipVel); PetscObjectSetName((PetscObject) _slipVel, "_slipVel");




  // frictional fields
  VecDuplicate(_tauQSP,&_sigma_N); PetscObjectSetName((PetscObject) _sigma_N, "_sigma_N");
  VecDuplicate(_tauQSP,&_zP); PetscObjectSetName((PetscObject) _zP, "_zP");
  VecDuplicate(_tauQSP,&_a); PetscObjectSetName((PetscObject) _a, "_a");
  VecDuplicate(_tauQSP,&_b); PetscObjectSetName((PetscObject) _b, "_b");


  setFrictionFields();

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending Fault::Fault in fault.cpp.\n");
#endif
}



// Check that required fields have been set by the input file
PetscErrorCode Fault::checkInput()
{
  PetscErrorCode ierr = 0;
  #if VERBOSE > 1
    ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting Domain::checkInputPlus in domain.cpp.\n");CHKERRQ(ierr);
  #endif

  assert(_Dc > 0 );
  assert(_aVals.size() == _aDepths.size() );
  assert(_bVals.size() == _bDepths.size() );
  assert(_sigmaNVals.size() == _sigmaNDepths.size() );


#if VERBOSE > 1
ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending Domain::checkInputPlus in domain.cpp.\n");CHKERRQ(ierr);
#endif
  //~}
  return ierr;
}


Fault::~Fault()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting Fault::~Fault in fault.cpp.\n");
#endif

  // fields that exist on the fault
  VecDestroy(&_tauQSP);
  VecDestroy(&_psi);
  VecDestroy(&_tempPsi);
  VecDestroy(&_dPsi);
  VecDestroy(&_slip);
  VecDestroy(&_slipVel);


  // frictional fields
  VecDestroy(&_zP);
  VecDestroy(&_a);
  VecDestroy(&_b);


  PetscViewerDestroy(&_slipViewer);
  PetscViewerDestroy(&_slipVelViewer);
  PetscViewerDestroy(&_tauQSPlusViewer);
  PetscViewerDestroy(&_psiViewer);

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending Fault::~Fault in fault.cpp.\n");
#endif
}

// Fills vec with the linear interpolation between the pairs of points (vals,depths).
PetscErrorCode Fault::setVecFromVectors(Vec& vec, vector<double>& vals,vector<double>& depths)
{
  PetscErrorCode ierr = 0;
  PetscInt       Ii,Istart,Iend;
  PetscScalar    v,z,z0,z1,v0,v1;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting Fault::setVecFromVectors in fault.cpp\n");CHKERRQ(ierr);
#endif

  // Find the appropriate starting pair of points to interpolate between: (z0,v0) and (z1,v1)
  z1 = depths.back();
  depths.pop_back();
  z0 = depths.back();
  v1 = vals.back();
  vals.pop_back();
  v0 = vals.back();
  ierr = VecGetOwnershipRange(vec,&Istart,&Iend);CHKERRQ(ierr);
  z = _h*(Iend-1);
  while (z<z0) {
    z1 = depths.back();
    depths.pop_back();
    z0 = depths.back();
    v1 = vals.back();
    vals.pop_back();
    v0 = vals.back();
    //~PetscPrintf(PETSC_COMM_WORLD,"2: z = %g: z0 = %g   z1 = %g   v0 = %g  v1 = %g\n",z,z0,z1,v0,v1);
  }


  for (Ii=Iend-1; Ii>=Istart; Ii--) {
    z = _h*Ii;
    if (z==z1) { v = v1; }
    else if (z==z0) { v = v0; }
    else if (z>z0 && z<z1) { v = (v1 - v0)/(z1-z0) * (z-z0) + v0; }

    // if z is no longer bracketed by (z0,z1), move on to the next pair of points
    if (z<=z0) {
      z1 = depths.back();
      depths.pop_back();
      z0 = depths.back();
      v1 = vals.back();
      vals.pop_back();
      v0 = vals.back();
    }
    ierr = VecSetValues(vec,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(vec);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(vec);CHKERRQ(ierr);

  //~VecView(vec,PETSC_VIEWER_STDOUT_WORLD);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending Fault::setVecFromVectors in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}

PetscErrorCode Fault::setFrictionFields()
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting Fault::setFrictionFields in fault.cpp\n");CHKERRQ(ierr);
#endif

  // set depth-independent fields
  ierr = VecSet(_psi,_f0);CHKERRQ(ierr);
  ierr = VecCopy(_psi,_tempPsi);CHKERRQ(ierr);

  // set a using a vals
  if (_N == 1) {
    VecSet(_b,_bVals[0]);
    VecSet(_a,_aVals[0]);
  }
  else {
    ierr = setVecFromVectors(_a,_aVals,_aDepths);CHKERRQ(ierr);
    ierr = setVecFromVectors(_b,_bVals,_bDepths);CHKERRQ(ierr);
    ierr = setVecFromVectors(_sigma_N,_sigmaNVals,_sigmaNDepths);CHKERRQ(ierr);
  }

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending Fault::setFrictionFields in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}



PetscScalar Fault::getTauInf(PetscInt& ind)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 2
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting Fault::getTauInf in fault.cpp for ind=%i\n",ind);CHKERRQ(ierr);
#endif

  PetscInt       Istart,Iend;
  PetscScalar    a,sigma_N;

  // throw error if value requested is not stored locally
  ierr = VecGetOwnershipRange(_tauQSP,&Istart,&Iend);CHKERRQ(ierr);
  assert(ind>=Istart && ind<Iend);

  ierr =  VecGetValues(_a,1,&ind,&a);CHKERRQ(ierr);
  ierr =  VecGetValues(_sigma_N,1,&ind,&sigma_N);CHKERRQ(ierr);

#if VERBOSE > 3
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending Fault::getTauInf in fault.cpp for ind=%i\n",ind);CHKERRQ(ierr);
#endif
  return sigma_N*a*asinh( (double) 0.5*_vL*exp(_f0/a)/_v0 );
}


PetscErrorCode Fault::agingLaw(const PetscInt ind,const PetscScalar psi,PetscScalar *dPsi)
{
  PetscErrorCode ierr = 0;
  PetscInt       Istart,Iend;
  PetscScalar    b,slipVel;


  ierr = VecGetOwnershipRange(_psi,&Istart,&Iend);
  assert( ind>=Istart && ind<Iend);
  ierr = VecGetValues(_b,1,&ind,&b);CHKERRQ(ierr);
  ierr = VecGetValues(_slipVel,1,&ind,&slipVel);CHKERRQ(ierr);
  slipVel = abs(slipVel); // aging law is not sensitive to direction of slip


  //~if (b==0) { *dPsi = 0; }
  if ( isinf(exp(1/b)) ) { *dPsi = 0; }
  else if ( b <= 1e-3 ) { *dPsi = 0; }
  else {
    *dPsi = (PetscScalar) (b*_v0/_Dc)*( exp((double) ( (_f0-psi)/b) ) - (slipVel/_v0) );
  }


  if (isnan(*dPsi)) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"isnan(*dPsi) evaluated to true\n");
    ierr = PetscPrintf(PETSC_COMM_WORLD,"psi=%.9e,b=%.9e,f0=%.9e,D_c=%.9e,v0=%.9e,vel=%.9e\n",psi,b,_f0,_Dc,_v0,slipVel);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"(b*D->v0/D->D_c)=%.9e\n",(b*_v0/_Dc));
    ierr = PetscPrintf(PETSC_COMM_WORLD,"exp((double) ( (D->f0-psi)/b) )=%.9e\n",exp((double) ( (_f0-psi)/b) ));
    ierr = PetscPrintf(PETSC_COMM_WORLD,"(vel/D->v0)=%.9e\n",(slipVel/_v0));
    CHKERRQ(ierr);
  }
  else if (isinf(*dPsi)) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"isinf(*dPsi) evaluated to true\n");
    ierr = PetscPrintf(PETSC_COMM_WORLD,"psi=%.9e,b=%.9e,f0=%.9e,D_c=%.9e,v0=%.9e,vel=%.9e\n",psi,b,_f0,_Dc,_v0,slipVel);
    CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"(b*D->v0/D->D_c)=%.9e\n",(b*_v0/_Dc));
    ierr = PetscPrintf(PETSC_COMM_WORLD,"exp((double) ( (D->f0-psi)/b) )=%.9e\n",exp((double) ( (_f0-psi)/b) ));
    ierr = PetscPrintf(PETSC_COMM_WORLD,"(vel/D->v0)=%.9e\n",(slipVel/_v0));
  }

  assert(!isnan(*dPsi));
  assert(!isinf(*dPsi));

  return ierr;
}




//================= Functions assuming only + side exists ========================
SymmFault::SymmFault(Domain&D)
: Fault(D)
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::SymmFault in fault.cpp.\n");
#endif

  // set up initial conditions for integration (shallow copy)
  _var.push_back(_psi);
  _var.push_back(_slip);

  // vectors were allocated in Fault constructor, just need to set values.

  setSplitNodeFields();

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::SymmFault in fault.cpp.\n");
#endif
}

SymmFault::~SymmFault()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::~SymmFault in fault.cpp.\n");
#endif

  // this is covered by the Fault destructor.

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::~SymmFault in fault.cpp.\n");
#endif
}

// assumes right-lateral fault
PetscErrorCode SymmFault::computeVel()
{
  PetscErrorCode ierr = 0;
  Vec            left,right,out;
  Vec            tauQS,eta;
  PetscScalar    outVal,leftVal,rightVal,temp;
  PetscInt       Ii,Istart,Iend;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::computeVel in fault.cpp\n");CHKERRQ(ierr);
#endif

  //~ierr = VecDuplicate(_tauQSP,&right);CHKERRQ(ierr);
  //~ierr = VecCopy(_tauQSP,right);CHKERRQ(ierr);
  //~ierr = VecPointwiseDivide(right,right,_zP);CHKERRQ(ierr);
  //~ierr = VecScale(right,2.0);CHKERRQ(ierr);
  //~ierr = VecAbs(right);CHKERRQ(ierr);

  // constructing right boundary: right = tauQS/eta
  //   tauQS = tauQSPlus
  //   eta = zPlus/2
  //   -> right = 2*tauQS/zPlus
  ierr = VecDuplicate(_tauQSP,&tauQS);CHKERRQ(ierr);
  ierr = VecDuplicate(_tauQSP,&eta);CHKERRQ(ierr);

  ierr = VecCopy(_tauQSP,tauQS);CHKERRQ(ierr);
  ierr = VecCopy(_zP,eta);
  ierr = VecScale(eta,0.5);CHKERRQ(ierr);

  // set up boundaries and output for rootfinder algorithm
  ierr = VecDuplicate(_tauQSP,&right);CHKERRQ(ierr);
  ierr = VecCopy(tauQS,right);CHKERRQ(ierr);
  ierr = VecPointwiseDivide(right,right,eta);CHKERRQ(ierr);

  ierr = VecDuplicate(right,&left);CHKERRQ(ierr);
  ierr = VecSet(left,0.0);CHKERRQ(ierr);

  ierr = VecDuplicate(left,&out);CHKERRQ(ierr);


  ierr = VecGetOwnershipRange(left,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    ierr = VecGetValues(left,1,&Ii,&leftVal);CHKERRQ(ierr);
    ierr = VecGetValues(right,1,&Ii,&rightVal);CHKERRQ(ierr);

    if (isnan(leftVal) || isnan(rightVal)) {
      PetscPrintf(PETSC_COMM_WORLD,"\n\nError:left or right evaluated to nan.\n");
    }
    // correct for left-lateral fault motion
    if (leftVal>rightVal) {
      temp = leftVal;
      rightVal = leftVal;
      leftVal = temp;
    }

    if (abs(leftVal-rightVal)<1e-14) { outVal = leftVal; }
    else {
      Bisect rootAlg(_maxNumIts,_rootTol);
      ierr = rootAlg.setBounds(leftVal,rightVal);CHKERRQ(ierr);
      ierr = rootAlg.findRoot(this,Ii,&outVal);CHKERRQ(ierr);
      _rootIts += rootAlg.getNumIts();
    }
    ierr = VecSetValue(_slipVel,Ii,outVal,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_slipVel);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_slipVel);CHKERRQ(ierr);


  ierr = VecDestroy(&tauQS);CHKERRQ(ierr);
  ierr = VecDestroy(&eta);CHKERRQ(ierr);
  ierr = VecDestroy(&left);CHKERRQ(ierr);
  ierr = VecDestroy(&right);CHKERRQ(ierr);
  ierr = VecDestroy(&out);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::computeVel in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}



// populate fields on the fault
PetscErrorCode SymmFault::setSplitNodeFields()
{
  PetscErrorCode ierr = 0;
  PetscInt       Ii,Istart,Iend;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::setSplitNodeFields in fault.cpp\n");CHKERRQ(ierr);
#endif


  // tau, eta, bcRShift, sigma_N
  PetscScalar a,b,zPlus,tau_inf,sigma_N;


  ierr = VecGetOwnershipRange(_tauQSP,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    ierr =  VecGetValues(_a,1,&Ii,&a);CHKERRQ(ierr);
    ierr =  VecGetValues(_b,1,&Ii,&b);CHKERRQ(ierr);
    ierr =  VecGetValues(_sigma_N,1,&Ii,&sigma_N);CHKERRQ(ierr);

    //eta = 0.5*sqrt(_rhoArr[Ii]*_muArr[Ii]);
    zPlus = _muArrPlus[Ii]/_csArrPlus[Ii];

    tau_inf = sigma_N*a*asinh( (double) 0.5*_vL*exp(_f0/a)/_v0 );
    //~bcRshift = tau_inf*_L/_muArrPlus[_sizeMuArr-_N+Ii]; // use last values of muArr

    ierr = VecSetValue(_tauQSP,Ii,tau_inf,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecSetValue(_zP,Ii,zPlus,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_tauQSP);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_zP);CHKERRQ(ierr);

  ierr = VecAssemblyEnd(_tauQSP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_zP);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::setSplitNodeFields in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}

// what is this function for??
PetscErrorCode SymmFault::setFaultDisp(Vec const &bcF, Vec const &bcFminus)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::setSymmFaultDisp in fault.cpp\n");CHKERRQ(ierr);
#endif

  // bcF holds displacement at y=0+
  ierr = VecCopy(bcF,_slip);CHKERRQ(ierr);
  ierr = VecScale(_slip,2.0);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::setSymmFault in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}

PetscErrorCode SymmFault::setTauQS(const Vec&sigma_xyPlus,const Vec& sigma_xyMinus)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::setTauQS in lithosphere.cpp.\n");CHKERRQ(ierr);
#endif

  PetscInt       Ii,Istart,Iend;
  PetscScalar    v;

  ierr = VecGetOwnershipRange(sigma_xyPlus,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    if (Ii<_N) {
      ierr = VecGetValues(sigma_xyPlus,1,&Ii,&v);CHKERRQ(ierr);
      ierr = VecSetValues(_tauQSP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_tauQSP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_tauQSP);CHKERRQ(ierr);


#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::setTauQS in lithosphere.c\n");CHKERRQ(ierr);
#endif
  return ierr;
}



PetscErrorCode SymmFault::getResid(const PetscInt ind,const PetscScalar slipVel,PetscScalar *out)
{
  PetscErrorCode ierr = 0;
  PetscScalar    psi,a,sigma_N,zPlus,tauQS;
  PetscInt       Istart,Iend;

#if VERBOSE > 3
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::getResid in fault.cpp\n");CHKERRQ(ierr);
#endif

  ierr = VecGetOwnershipRange(_psi,&Istart,&Iend);
  assert(ind>=Istart && ind<Iend);
  ierr = VecGetValues(_tempPsi,1,&ind,&psi);CHKERRQ(ierr);
  ierr = VecGetValues(_a,1,&ind,&a);CHKERRQ(ierr);
  ierr = VecGetValues(_sigma_N,1,&ind,&sigma_N);CHKERRQ(ierr);
  ierr = VecGetValues(_zP,1,&ind,&zPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_tauQSP,1,&ind,&tauQS);CHKERRQ(ierr);

  // frictional strength of fault
  PetscScalar strength = (PetscScalar) a*sigma_N*asinh( (double) (slipVel/2./_v0)*exp(psi/a) );

  // stress on fault
  PetscScalar stress = tauQS - 0.5*zPlus*slipVel;

  *out = strength - stress;
  //~*out = (PetscScalar) a*sigma_N*asinh( (double) (vel/2./_v0)*exp(psi/a) ) + 0.5*zPlus*slipVel - tauQS;

#if VERBOSE > 3
  ierr = PetscPrintf(PETSC_COMM_WORLD,"    psi=%g,a=%g,sigma_n=%g,z=%g,tau=%g,vel=%g\n",psi,a,sigma_N,zPlus,tauQS,slipVel);
#endif
  if (isnan(*out)) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"isnan(*out) evaluated to true\n");
    ierr = PetscPrintf(PETSC_COMM_WORLD,"psi=%g,a=%g,sigma_n=%g,eta=%g,tau=%g,vel=%g\n",psi,a,sigma_N,zPlus,tauQS,slipVel);
    CHKERRQ(ierr);
  }
  else if (isinf(*out)) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"isinf(*out) evaluated to true\n");
    ierr = PetscPrintf(PETSC_COMM_WORLD,"psi=%g,a=%g,sigma_n=%g,eta=%g,tau=%g,vel=%g\n",psi,a,sigma_N,zPlus,tauQS,slipVel);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"(vel/2/_v0)=%.9e\n",slipVel/2/_v0);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"exp(psi/a)=%.9e\n",exp(psi/a));
    ierr = PetscPrintf(PETSC_COMM_WORLD,"eta*vel=%.9e\n",zPlus*slipVel);
    CHKERRQ(ierr);
  }

  assert(!isnan(*out));
  assert(!isinf(*out));

#if VERBOSE > 3
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::getResid in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}





PetscErrorCode SymmFault::d_dt(const_it_vec varBegin,const_it_vec varEnd,
                        it_vec dvarBegin,it_vec dvarEnd)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::d_dt in fault.cpp\n");CHKERRQ(ierr);
#endif

  PetscScalar    val,psiVal;
  PetscInt       Ii,Istart,Iend;

  assert(varBegin+1 != varEnd);

  ierr = VecCopy(*(varBegin),_tempPsi);CHKERRQ(ierr);
  ierr = computeVel();CHKERRQ(ierr);

  ierr = VecGetOwnershipRange(_slipVel,&Istart,&Iend);
  for (Ii=Istart;Ii<Iend;Ii++) {
    ierr = VecGetValues(*(varBegin),1,&Ii,&psiVal);
    ierr = agingLaw(Ii,psiVal,&val);CHKERRQ(ierr);
    ierr = VecSetValue(*(dvarBegin),Ii,val,INSERT_VALUES);CHKERRQ(ierr);

    ierr = VecGetValues(_slipVel,1,&Ii,&val);CHKERRQ(ierr);
    ierr = VecSetValue(*(dvarBegin+1),Ii,val,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(*dvarBegin);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(*(dvarBegin+1));CHKERRQ(ierr);

  ierr = VecAssemblyEnd(*dvarBegin);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(*(dvarBegin+1));CHKERRQ(ierr);

  //~// force fault to remain locked
  //~ierr = VecSet(*dvarBegin,0.0);CHKERRQ(ierr);
  //~ierr = VecSet(*(dvarBegin+1),0.0);CHKERRQ(ierr);


#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::d_dt in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode SymmFault::writeContext(const string outputDir)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting SymmFault::writeContext in fault.cpp\n");CHKERRQ(ierr);
#endif

  PetscViewer    viewer;

  std::string str = outputDir + "a";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_a,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  str = outputDir + "b";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_b,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  str = outputDir + "zPlus";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_zP,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  // output normal stress vector
  str =  outputDir + "sigma_N";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_sigma_N,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);


#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::writeContext in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode SymmFault::writeStep(const string outputDir,const PetscInt step)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"starting SymmFault::writeStep in fault.cpp at step %i\n",step);CHKERRQ(ierr);
#endif

  if (step==0) {
      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"slip").c_str(),FILE_MODE_WRITE,&_slipViewer);
      ierr = VecView(_slip,_slipViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_slipViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"slip").c_str(),
                                   FILE_MODE_APPEND,&_slipViewer);CHKERRQ(ierr);

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"slipVel").c_str(),FILE_MODE_WRITE,&_slipVelViewer);
      ierr = VecView(_slipVel,_slipVelViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_slipVelViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"slipVel").c_str(),
                                   FILE_MODE_APPEND,&_slipVelViewer);CHKERRQ(ierr);

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"tauQSPlus").c_str(),FILE_MODE_WRITE,&_tauQSPlusViewer);
      ierr = VecView(_tauQSP,_tauQSPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_tauQSPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"tauQSPlus").c_str(),
                                   FILE_MODE_APPEND,&_tauQSPlusViewer);CHKERRQ(ierr);

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"psi").c_str(),FILE_MODE_WRITE,&_psiViewer);
      ierr = VecView(_psi,_psiViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_psiViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"psi").c_str(),
                                   FILE_MODE_APPEND,&_psiViewer);CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_slip,_slipViewer);CHKERRQ(ierr);
    ierr = VecView(_slipVel,_slipVelViewer);CHKERRQ(ierr);
    ierr = VecView(_tauQSP,_tauQSPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_psi,_psiViewer);CHKERRQ(ierr);
  }

#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending SymmFault::writeStep in fault.cpp at step %i\n",step);CHKERRQ(ierr);
#endif
  return ierr;
}











//================= FullFault Functions (both + and - sides) ===========
FullFault::FullFault(Domain&D)
: Fault(D),_zM(NULL),_muArrMinus(D._muArrMinus),_csArrMinus(D._csArrMinus),
  _arrSize(D._Ny*D._Nz),
  _uP(NULL),_uM(NULL),_velPlus(NULL),_velMinus(NULL),
  _uPlusViewer(NULL),_uMinusViewer(NULL),_velPlusViewer(NULL),_velMinusViewer(NULL),
 _tauQSMinusViewer(NULL),_psiViewer(NULL),_tauQSMinus(NULL)
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::FullFault in fault.cpp.\n");
#endif

  // allocate space for new fields that exist on left ndoes
  VecDuplicate(_tauQSP,&_tauQSMinus); PetscObjectSetName((PetscObject) _tauQSMinus, "tauQSminus");
  VecDuplicate(_tauQSP,&_uP);  PetscObjectSetName((PetscObject) _uP, "uPlus");
  VecDuplicate(_tauQSP,&_uM);  PetscObjectSetName((PetscObject) _uM, "uMinus");
  VecDuplicate(_tauQSP,&_velMinus); PetscObjectSetName((PetscObject) _velMinus, "velMinus");
  VecDuplicate(_tauQSP,&_velPlus); PetscObjectSetName((PetscObject) _velPlus, "velPlus");
  VecDuplicate(_tauQSP,&_zM); PetscObjectSetName((PetscObject) _zM, "zMinus");

  _var.push_back(_psi);
  _var.push_back(_uP);
  _var.push_back(_uM);

  setSplitNodeFields();

#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::FullFault in fault.cpp.\n");
#endif
}




PetscErrorCode Fault::loadSettings(const char *file)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting loadData in domain.cpp, loading from file: %s.\n", file);CHKERRQ(ierr);
#endif
  PetscMPIInt rank,size;
  MPI_Comm_size(PETSC_COMM_WORLD,&size);
  MPI_Comm_rank(PETSC_COMM_WORLD,&rank);


  ifstream infile( file );
  string line,var;
  size_t pos = 0;
  while (getline(infile, line))
  {
    istringstream iss(line);
    pos = line.find(_delim); // find position of the delimiter
    var = line.substr(0,pos);

    if (var.compare("Dc")==0) { _Dc = atof( (line.substr(pos+_delim.length(),line.npos)).c_str() ); }

    //fault properties
    else if (var.compare("sigmaNVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_sigmaNVals);
      //~_aLen = _aVals.size();
    }
    else if (var.compare("sigmaNDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_sigmaNDepths);
    }

    else if (var.compare("aVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_aVals);
      //~_aLen = _aVals.size();
    }
    else if (var.compare("aDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_aDepths);
    }
    else if (var.compare("bVals")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_bVals);
    }
    else if (var.compare("bDepths")==0) {
      string str = line.substr(pos+_delim.length(),line.npos);
      loadVectorFromInputFile(str,_bDepths);
    }
  }

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending loadData in domain.cpp.\n");CHKERRQ(ierr);
#endif
  return ierr;
}




FullFault::~FullFault()
{
#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::~FullFault in fault.cpp.\n");
#endif

  // fields that exist on the - side of the split nodes
  VecDestroy(&_tauQSMinus);

  VecDestroy(&_zM);
  VecDestroy(&_uM);
  VecDestroy(&_uP);
  VecDestroy(&_velMinus);
  VecDestroy(&_velPlus);

  PetscViewerDestroy(&_uPlusViewer);
  PetscViewerDestroy(&_uMinusViewer);
  PetscViewerDestroy(&_velMinusViewer);
  PetscViewerDestroy(&_velPlusViewer);
  PetscViewerDestroy(&_tauQSMinusViewer);


#if VERBOSE > 1
  PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::~FullFault in fault.cpp.\n");
#endif
}


//==================== protected member functions ======================
// compute slipVel ( = velPlus - velMinus), assuming right-lateral fault
PetscErrorCode FullFault::computeVel()
{
  PetscErrorCode ierr = 0;
  Vec            left=NULL,right=NULL,out=NULL;
  Vec            zSum=NULL,temp=NULL,tauQS=NULL,eta=NULL;
  PetscScalar    outVal=0,leftVal=0,rightVal=0;
  PetscInt       Ii,Istart,Iend;

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::computeVel in fault.cpp\n");CHKERRQ(ierr);
#endif

  // constructing right boundary: right = tauQS/eta
  // for full fault:
  //   tauQS = [zMinus*tauQSplus + zPlus*tauQSminus]/(zPlus + zMinus)
  //   eta = zPlus*zMinus/(zPlus+zMinus)
  ierr = VecDuplicate(_tauQSP,&tauQS);CHKERRQ(ierr);
  ierr = VecDuplicate(_tauQSP,&eta);CHKERRQ(ierr);

  // zSum = zPlus + zMinus
  ierr = VecDuplicate(_zP,&zSum);CHKERRQ(ierr);
  ierr = VecCopy(_zP,zSum);CHKERRQ(ierr);
  ierr = VecAXPY(zSum,1.0,_zM);CHKERRQ(ierr);

  // construct eta
  ierr = VecPointwiseMult(eta,_zP,_zM);CHKERRQ(ierr);
  ierr = VecPointwiseDivide(eta,eta,zSum);CHKERRQ(ierr);

  // construct tauQS
  ierr = VecPointwiseDivide(tauQS,_zM,zSum);CHKERRQ(ierr);
  ierr = VecPointwiseMult(tauQS,tauQS,_tauQSP);CHKERRQ(ierr);
  ierr = VecDuplicate(_tauQSP,&temp);CHKERRQ(ierr);
  ierr = VecPointwiseDivide(temp,_zP,zSum);CHKERRQ(ierr);
  ierr = VecPointwiseMult(temp,temp,_tauQSMinus);CHKERRQ(ierr);
  ierr = VecAXPY(tauQS,1.0,temp);CHKERRQ(ierr); // tauQS = tauQS + temp2
  ierr = VecDestroy(&temp);CHKERRQ(ierr);


  // set up boundaries and output for rootfinder algorithm
  ierr = VecDuplicate(_tauQSP,&right);CHKERRQ(ierr);
  ierr = VecCopy(tauQS,right);CHKERRQ(ierr);
  ierr = VecPointwiseDivide(right,right,eta);CHKERRQ(ierr);


  //~// from SymmFault, here for debugging purposes
  //~ierr = VecDuplicate(_tauQSP,&tauQS);CHKERRQ(ierr);
  //~ierr = VecDuplicate(_tauQSP,&eta);CHKERRQ(ierr);
  //~ierr = VecCopy(_tauQSP,tauQS);CHKERRQ(ierr);
  //~ierr = VecCopy(_zP,eta);
  //~ierr = VecScale(eta,0.5);CHKERRQ(ierr);

  ierr = VecDuplicate(right,&left);CHKERRQ(ierr);
  ierr = VecSet(left,0.0);CHKERRQ(ierr);

  ierr = VecDuplicate(left,&out);CHKERRQ(ierr);
  ierr = VecSet(out,0.0);CHKERRQ(ierr);

  ierr = VecGetOwnershipRange(left,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    ierr = VecGetValues(left,1,&Ii,&leftVal);CHKERRQ(ierr);
    ierr = VecGetValues(right,1,&Ii,&rightVal);CHKERRQ(ierr);
    if (abs(leftVal-rightVal)<1e-14) { outVal = leftVal; }
    else {
      // construct fresh each time so the boundaries etc are correct
      Bisect rootAlg(_maxNumIts,_rootTol);
      ierr = rootAlg.setBounds(leftVal,rightVal);CHKERRQ(ierr);
      ierr = rootAlg.findRoot(this,Ii,&outVal);CHKERRQ(ierr);
      _rootIts += rootAlg.getNumIts();
    }
    ierr = VecSetValue(_slipVel,Ii,outVal,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(_slipVel);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_slipVel);CHKERRQ(ierr);

  // compute velPlus
  // velPlus = (+tauQSplus - tauQSminus + zMinus*vel)/(zPlus + zMinus)
  Vec tauSum=NULL, velCorr=NULL;
  VecDuplicate(_slipVel,&tauSum);
  VecCopy(_tauQSP,tauSum);
  VecAXPY(tauSum,-1.0,_tauQSMinus);
  VecPointwiseDivide(tauSum,tauSum,zSum);
  VecDuplicate(_slipVel,&velCorr);
  VecPointwiseDivide(velCorr,_zM,zSum);
  VecPointwiseMult(velCorr,velCorr,_slipVel);
  VecCopy(tauSum,_velPlus);
  VecAXPY(_velPlus,1.0,velCorr);

  // compute velMinus
  ierr = VecCopy(_velPlus,_velMinus);CHKERRQ(ierr);
  ierr = VecAXPY(_velMinus,-1.0,_slipVel);CHKERRQ(ierr);


  //~// from SymmFault, here for debugging purposes
  //~ierr = VecCopy(_slipVel,_velPlus);CHKERRQ(ierr);
  //~ierr = VecScale(_velPlus,0.5);CHKERRQ(ierr);
  //~ierr = VecCopy(_velPlus,_velMinus);CHKERRQ(ierr);
  //~ierr = VecScale(_velMinus,-1.0);CHKERRQ(ierr);


  // clean up memory (this step is a common source of memory leaks)
  ierr = VecDestroy(&tauSum);CHKERRQ(ierr);
  ierr = VecDestroy(&velCorr);CHKERRQ(ierr);
  ierr = VecDestroy(&zSum);CHKERRQ(ierr);
  ierr = VecDestroy(&tauQS);CHKERRQ(ierr);
  ierr = VecDestroy(&eta);CHKERRQ(ierr);
  ierr = VecDestroy(&left);CHKERRQ(ierr);
  ierr = VecDestroy(&right);CHKERRQ(ierr);
  ierr = VecDestroy(&out);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::computeVel in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode FullFault::setSplitNodeFields()
{
  PetscErrorCode ierr = 0;
  PetscInt       Ii,Istart,Iend;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::setSplitNodeFields in fault.cpp\n");CHKERRQ(ierr);
#endif


  // tauQSPlus/Minus, zPlus/Minus, bcRShift, sigma_N
  PetscScalar a,b,zPlus,zMinus,tau_inf,sigma_N;
  ierr = VecGetOwnershipRange(_tauQSP,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    ierr =  VecGetValues(_a,1,&Ii,&a);CHKERRQ(ierr);
    ierr =  VecGetValues(_b,1,&Ii,&b);CHKERRQ(ierr);
    ierr =  VecGetValues(_sigma_N,1,&Ii,&sigma_N);CHKERRQ(ierr);

    //~eta = 0.5*sqrt(_rhoArr[Ii]*_muArr[Ii]);
    zPlus = _muArrPlus[Ii]/_csArrPlus[Ii];
    //~zMinus = _muArrMinus[Ii]/_csArrMinus[Ii];
    zMinus = _muArrMinus[_arrSize - _N + Ii]/_csArrMinus[_arrSize - _N + Ii];

    tau_inf = sigma_N*a*asinh( (double) 0.5*_vL*exp(_f0/a)/_v0 );

    ierr = VecSetValue(_tauQSP,Ii,tau_inf,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecSetValue(_tauQSMinus,Ii,tau_inf,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecSetValue(_zP,Ii,zPlus,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecSetValue(_zM,Ii,zMinus,INSERT_VALUES);CHKERRQ(ierr);
    ierr = VecSetValue(_sigma_N,Ii,sigma_N,INSERT_VALUES);CHKERRQ(ierr);
  }

  ierr = VecAssemblyBegin(_tauQSP);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_tauQSMinus);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_zP);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_zM);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(_sigma_N);CHKERRQ(ierr);

  ierr = VecAssemblyEnd(_tauQSP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_tauQSMinus);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_zP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_zM);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_sigma_N);CHKERRQ(ierr);



#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::setSplitNodeFields in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


// initialize uPlus and uMinus from lithosphere's data
PetscErrorCode FullFault::setFaultDisp(Vec const &bcLPlus,Vec const &bcRMinus)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::setFullFaultDisp in fault.cpp\n");CHKERRQ(ierr);
#endif

    ierr = VecCopy(bcLPlus,_uP);CHKERRQ(ierr);
    ierr = VecCopy(bcRMinus,_uM);CHKERRQ(ierr);

#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::setFullFaultDisp in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode FullFault::setTauQS(const Vec& sigma_xyPlus,const Vec& sigma_xyMinus)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::setTauQS in lithosphere.cpp.\n");CHKERRQ(ierr);
#endif

  PetscInt       Ii,Istart,Iend,Jj;
  PetscScalar    v;

  ierr = VecGetOwnershipRange(sigma_xyPlus,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    if (Ii<_N) {
      ierr = VecGetValues(sigma_xyPlus,1,&Ii,&v);CHKERRQ(ierr);
      ierr = VecSetValues(_tauQSP,1,&Ii,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }
  ierr = VecAssemblyBegin(_tauQSP);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(_tauQSP);CHKERRQ(ierr);

  // get last _N values in array
  ierr = VecGetOwnershipRange(sigma_xyMinus,&Istart,&Iend);CHKERRQ(ierr);
  for (Ii=Istart;Ii<Iend;Ii++) {
    if (Ii>_arrSize - _N - 1) {
      Jj = Ii - (_arrSize - _N);
      ierr = VecGetValues(sigma_xyMinus,1,&Ii,&v);CHKERRQ(ierr);
      ierr = VecSetValues(_tauQSMinus,1,&Jj,&v,INSERT_VALUES);CHKERRQ(ierr);
    }
  }


#if VERBOSE > 1
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::setTauQS in lithosphere.c\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode FullFault::getResid(const PetscInt ind,const PetscScalar slipVel,PetscScalar *out)
{
  PetscErrorCode ierr = 0;
  PetscScalar    psi,a,sigma_N,zPlus,zMinus,tauQSplus,tauQSminus;
  PetscInt       Istart,Iend;

#if VERBOSE > 3
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::getResid in fault.cpp\n");CHKERRQ(ierr);
#endif

  ierr = VecGetOwnershipRange(_psi,&Istart,&Iend);
  assert( ind>=Istart && ind<Iend );

  ierr = VecGetValues(_tempPsi,1,&ind,&psi);CHKERRQ(ierr);
  ierr = VecGetValues(_a,1,&ind,&a);CHKERRQ(ierr);
  ierr = VecGetValues(_sigma_N,1,&ind,&sigma_N);CHKERRQ(ierr);

  ierr = VecGetValues(_zP,1,&ind,&zPlus);CHKERRQ(ierr);
  ierr = VecGetValues(_tauQSP,1,&ind,&tauQSplus);CHKERRQ(ierr);

  ierr = VecGetValues(_tauQSMinus,1,&ind,&tauQSminus);CHKERRQ(ierr);
  ierr = VecGetValues(_zM,1,&ind,&zMinus);CHKERRQ(ierr);

  // frictional strength of fault
  PetscScalar strength = (PetscScalar) a*sigma_N*asinh( (double) (slipVel/2/_v0)*exp(psi/a) );

  // stress on fault
  PetscScalar stress = (zMinus/(zPlus+zMinus)*tauQSplus
                       + zPlus/(zPlus+zMinus)*tauQSminus)
                       - zPlus*zMinus/(zPlus+zMinus)*slipVel;

  // from symmetric fault (here for debugging purposes)
  //~stress = tauQSplus - 0.5*zPlus*slipVel; // stress on fault

  *out = strength - stress;

#if VERBOSE > 3
  ierr = PetscPrintf(PETSC_COMM_WORLD,"    psi=%g,a=%g,sigma_n=%g,zPlus=%g,tau=%g,vel=%g,out=%g\n",psi,a,sigma_N,zPlus,tauQSplus,vel,out);
#endif
  if (isnan(*out)) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"isnan(*out) evaluated to true\n");
    ierr = PetscPrintf(PETSC_COMM_WORLD,"psi=%g,a=%g,sigma_n=%g,eta=%g,tau=%g,vel=%g\n",psi,a,sigma_N,zPlus,tauQSplus,slipVel);
    CHKERRQ(ierr);
  }
  else if (isinf(*out)) {
    ierr = PetscPrintf(PETSC_COMM_WORLD,"isinf(*out) evaluated to true\n");
    ierr = PetscPrintf(PETSC_COMM_WORLD,"psi=%g,a=%g,sigma_n=%g,eta=%g,tau=%g,vel=%g\n",psi,a,sigma_N,zPlus,tauQSplus,slipVel);
    CHKERRQ(ierr);
  }

  assert(!isnan(*out));
  assert(!isinf(*out));

#if VERBOSE > 3
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::getResid in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}







PetscErrorCode FullFault::d_dt(const_it_vec varBegin,const_it_vec varEnd,
                               it_vec dvarBegin,it_vec dvarEnd)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 3
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::d_dt in fault.cpp\n");CHKERRQ(ierr);
#endif

  PetscScalar    val,psiVal;
  PetscInt       Ii,Istart,Iend;

  assert(varBegin+1 != varEnd);

  ierr = VecCopy(*(varBegin),_tempPsi);CHKERRQ(ierr);
  ierr = computeVel();CHKERRQ(ierr);

  ierr = VecGetOwnershipRange(_slipVel,&Istart,&Iend);
  for (Ii=Istart;Ii<Iend;Ii++) {
    ierr = VecGetValues(*varBegin,1,&Ii,&psiVal);
    ierr = agingLaw(Ii,psiVal,&val);CHKERRQ(ierr);
    ierr = VecSetValue(*dvarBegin,Ii,val,INSERT_VALUES);CHKERRQ(ierr);

    ierr = VecGetValues(_velPlus,1,&Ii,&val);CHKERRQ(ierr);
    ierr = VecSetValue(*(dvarBegin+1),Ii,val,INSERT_VALUES);CHKERRQ(ierr);

    ierr = VecGetValues(_velMinus,1,&Ii,&val);CHKERRQ(ierr);
    ierr = VecSetValue(*(dvarBegin+2),Ii,val,INSERT_VALUES);CHKERRQ(ierr);
  }
  ierr = VecAssemblyBegin(*dvarBegin);CHKERRQ(ierr);
  ierr = VecAssemblyBegin(*(dvarBegin+1));CHKERRQ(ierr);
  ierr = VecAssemblyBegin(*(dvarBegin+2));CHKERRQ(ierr);

  ierr = VecAssemblyEnd(*dvarBegin);CHKERRQ(ierr);
  ierr = VecAssemblyEnd(*(dvarBegin+1));CHKERRQ(ierr);
  ierr = VecAssemblyEnd(*(dvarBegin+2));CHKERRQ(ierr);

  return ierr;
#if VERBOSE > 3
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::d_dt in fault.cpp\n");CHKERRQ(ierr);
#endif
}


PetscErrorCode FullFault::writeContext(const string outputDir)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Starting FullFault::writeContext in fault.cpp\n");CHKERRQ(ierr);
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

  std::string str = outputDir + "a";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_a,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  str = outputDir + "b";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_b,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  str = outputDir + "zPlus";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_zP,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  str = outputDir + "zMinus";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_zM,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

  // output normal stress vector
  str =  outputDir + "sigma_N";
  ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,str.c_str(),FILE_MODE_WRITE,&viewer);CHKERRQ(ierr);
  ierr = VecView(_sigma_N,viewer);CHKERRQ(ierr);
  ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);

#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::writeContext in fault.cpp\n");CHKERRQ(ierr);
#endif
  return ierr;
}


PetscErrorCode FullFault::writeStep(const string outputDir,const PetscInt step)
{
  PetscErrorCode ierr = 0;
#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"starting FullFault::writeStep in fault.cpp at step %i\n",step);CHKERRQ(ierr);
#endif

  if (step==0) {

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"uPlus").c_str(),FILE_MODE_WRITE,&_uPlusViewer);
      ierr = VecView(_uP,_uPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_uPlusViewer);CHKERRQ(ierr);

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"velPlus").c_str(),FILE_MODE_WRITE,&_velPlusViewer);
      ierr = VecView(_velPlus,_velPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_velPlusViewer);CHKERRQ(ierr);

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"tauQSPlus").c_str(),FILE_MODE_WRITE,&_tauQSPlusViewer);
      ierr = VecView(_tauQSP,_tauQSPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_tauQSPlusViewer);CHKERRQ(ierr);

      PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"psi").c_str(),FILE_MODE_WRITE,&_psiViewer);
      ierr = VecView(_psi,_psiViewer);CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&_psiViewer);CHKERRQ(ierr);

      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"uPlus").c_str(),
                                   FILE_MODE_APPEND,&_uPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"velPlus").c_str(),
                                   FILE_MODE_APPEND,&_velPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"tauQSPlus").c_str(),
                                   FILE_MODE_APPEND,&_tauQSPlusViewer);CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"psi").c_str(),
                                   FILE_MODE_APPEND,&_psiViewer);CHKERRQ(ierr);

        PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"uMinus").c_str(),FILE_MODE_WRITE,&_uMinusViewer);
        ierr = VecView(_uM,_uMinusViewer);CHKERRQ(ierr);
        ierr = PetscViewerDestroy(&_uMinusViewer);CHKERRQ(ierr);

        PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"tauQSMinus").c_str(),FILE_MODE_WRITE,&_tauQSMinusViewer);
        ierr = VecView(_tauQSMinus,_tauQSMinusViewer);CHKERRQ(ierr);
        ierr = PetscViewerDestroy(&_tauQSMinusViewer);CHKERRQ(ierr);

        PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"velMinus").c_str(),FILE_MODE_WRITE,&_velMinusViewer);
        ierr = VecView(_velMinus,_velMinusViewer);CHKERRQ(ierr);
        ierr = PetscViewerDestroy(&_velMinusViewer);CHKERRQ(ierr);

        ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"uMinus").c_str(),
                                   FILE_MODE_APPEND,&_uMinusViewer);CHKERRQ(ierr);
        ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"velMinus").c_str(),
                                   FILE_MODE_APPEND,&_velMinusViewer);CHKERRQ(ierr);
        ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,(outputDir+"tauQSMinus").c_str(),
                                   FILE_MODE_APPEND,&_tauQSMinusViewer);CHKERRQ(ierr);
  }
  else {
    ierr = VecView(_uP,_uPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_velPlus,_velPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_tauQSP,_tauQSPlusViewer);CHKERRQ(ierr);
    ierr = VecView(_psi,_psiViewer);CHKERRQ(ierr);

      ierr = VecView(_uM,_uMinusViewer);CHKERRQ(ierr);
      ierr = VecView(_velMinus,_velMinusViewer);CHKERRQ(ierr);
      ierr = VecView(_tauQSMinus,_tauQSMinusViewer);CHKERRQ(ierr);
  }

#if VERBOSE > 1
   ierr = PetscPrintf(PETSC_COMM_WORLD,"Ending FullFault::writeStep in fault.cpp at step %i\n",step);CHKERRQ(ierr);
#endif
  return ierr;
}



