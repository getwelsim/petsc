/*$Id: ex5.c,v 1.142 2001/08/07 21:31:17 bsmith Exp $*/

/* Program usage:  mpirun -np <procs> ex5 [-help] [all PETSc options] */

static char help[] = "Bratu nonlinear PDE in 2d.\n\
We solve the  Bratu (SFI - solid fuel ignition) problem in a 2D rectangular\n\
domain, using distributed arrays (DAs) to partition the parallel grid.\n\
The command line options include:\n\
  -par <parameter>, where <parameter> indicates the problem's nonlinearity\n\
     problem SFI:  <parameter> = Bratu parameter (0 <= par <= 6.81)\n\n";

/*T
   Concepts: SNES^parallel Bratu example
   Concepts: DA^using distributed arrays;
   Processors: n
T*/

/* ------------------------------------------------------------------------

    Solid Fuel Ignition (SFI) problem.  This problem is modeled by
    the partial differential equation
  
            -Laplacian u - lambda*exp(u) = 0,  0 < x,y < 1,
  
    with boundary conditions
   
             u = 0  for  x = 0, x = 1, y = 0, y = 1.
  
    A finite difference approximation with the usual 5-point stencil
    is used to discretize the boundary value problem to obtain a nonlinear 
    system of equations.


  ------------------------------------------------------------------------- */

/* 
   Include "petscda.h" so that we can use distributed arrays (DAs).
   Include "petscsnes.h" so that we can use SNES solvers.  Note that this
   file automatically includes:
     petsc.h       - base PETSc routines   petscvec.h - vectors
     petscsys.h    - system routines       petscmat.h - matrices
     petscis.h     - index sets            petscksp.h - Krylov subspace methods
     petscviewer.h - viewers               petscpc.h  - preconditioners
     petscsles.h   - linear solvers
*/
#include "petscda.h"
#include "petscsnes.h"

/* 
   User-defined application context - contains data needed by the 
   application-provided call-back routines, FormJacobian() and
   FormFunction().
*/
typedef struct {
   DA            da;             /* distributed array data structure */
   PassiveReal param;          /* test problem parameter */
} AppCtx;

/* 
   User-defined routines
*/
extern int FormFunction(SNES,Vec,Vec,void*),FormInitialGuess(AppCtx*,Vec),FormFunctionMatlab(SNES,Vec,Vec,void*);
extern int FormJacobian(SNES,Vec,Mat*,Mat*,MatStructure*,void*);
extern int FormFunctionLocal(DALocalInfo*,PetscScalar**,PetscScalar**,AppCtx*);
extern int FormJacobianLocal(DALocalInfo*,PetscScalar**,Mat,AppCtx*);

#undef __FUNCT__
#define __FUNCT__ "main"
int main(int argc,char **argv)
{
  SNES                   snes;                 /* nonlinear solver */
  Vec                    x,r;                  /* solution, residual vectors */
  Mat                    A,J;                    /* Jacobian matrix */
  AppCtx                 user;                 /* user-defined work context */
  int                    its;                  /* iterations for convergence */
  PetscTruth             local_function = PETSC_TRUE,global_function = PETSC_FALSE,matlab_function = PETSC_FALSE;
  PetscTruth             fd_jacobian = PETSC_FALSE,global_jacobian=PETSC_FALSE,local_jacobian=PETSC_TRUE,adic_jacobian=PETSC_FALSE;
  PetscTruth             adicmf_jacobian = PETSC_FALSE;
  int                    ierr;
  PetscReal              bratu_lambda_max = 6.81,bratu_lambda_min = 0.,fnorm;
  MatFDColoring          matfdcoloring = 0;
  ISColoring             iscoloring;


   
  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Initialize program
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  PetscInitialize(&argc,&argv,(char *)0,help);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Initialize problem parameters
  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  user.param = 6.0;
  ierr = PetscOptionsGetReal(PETSC_NULL,"-par",&user.param,PETSC_NULL);CHKERRQ(ierr);
  if (user.param >= bratu_lambda_max || user.param <= bratu_lambda_min) {
    SETERRQ(1,"Lambda is out of range");
  }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create nonlinear solver context
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create distributed array (DA) to manage parallel grid and vectors
  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = DACreate2d(PETSC_COMM_WORLD,DA_NONPERIODIC,DA_STENCIL_STAR,-4,-4,PETSC_DECIDE,PETSC_DECIDE,
                    1,1,PETSC_NULL,PETSC_NULL,&user.da);CHKERRQ(ierr);

  /*  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Extract global vectors from DA; then duplicate for remaining
     vectors that are the same types
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = DACreateGlobalVector(user.da,&x);CHKERRQ(ierr);
  ierr = VecDuplicate(x,&r);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Set local function evaluation routine
  - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = DASetLocalFunction(user.da,(DALocalFunction1)FormFunctionLocal);CHKERRQ(ierr);
  ierr = DASetLocalJacobian(user.da,(DALocalFunction1)FormJacobianLocal);CHKERRQ(ierr);
  ierr = DASetLocalAdicFunction(user.da,ad_FormFunctionLocal);CHKERRQ(ierr);

  /* Decide which FormFunction to use */
  ierr = PetscOptionsGetLogical(PETSC_NULL,"-matlab_function",&matlab_function,0);CHKERRQ(ierr);
  ierr = PetscOptionsGetLogical(PETSC_NULL,"-global_function",&global_function,0);CHKERRQ(ierr);
  ierr = PetscOptionsGetLogical(PETSC_NULL,"-local_function",&local_function,0);CHKERRQ(ierr);

  if (global_function) {
    ierr = SNESSetFunction(snes,r,FormFunction,&user);CHKERRQ(ierr);
#if defined(PETSC_HAVE_MATLAB_ENGINE) && !defined(PETSC_USE_COMPLEX) && !defined(PETSC_USE_SINGLE)
  } else if (matlab_function) {
    ierr = SNESSetFunction(snes,r,FormFunctionMatlab,&user);CHKERRQ(ierr);
#endif
  } else if (local_function) {
    ierr = SNESSetFunction(snes,r,SNESDAFormFunction,&user);CHKERRQ(ierr);
  }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Create matrix data structure; set Jacobian evaluation routine

     Set Jacobian matrix data structure and default Jacobian evaluation
     routine. User can override with:
     -snes_mf : matrix-free Newton-Krylov method with no preconditioning
                (unless user explicitly sets preconditioner) 
     -snes_mf_operator : form preconditioning matrix as set by the user,
                         but use matrix-free approx for Jacobian-vector
                         products within Newton-Krylov method

     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = DAGetMatrix(user.da,MATMPIAIJ,&J);CHKERRQ(ierr);
  A    = J;

  ierr = PetscOptionsGetLogical(PETSC_NULL,"-fd_jacobian",&fd_jacobian,0);CHKERRQ(ierr);
  ierr = PetscOptionsGetLogical(PETSC_NULL,"-adic_jacobian",&adic_jacobian,0);CHKERRQ(ierr);
  ierr = PetscOptionsGetLogical(PETSC_NULL,"-global_jacobian",&global_jacobian,0);CHKERRQ(ierr);
  ierr = PetscOptionsGetLogical(PETSC_NULL,"-local_jacobian",&local_jacobian,0);CHKERRQ(ierr);

  ierr = PetscOptionsGetLogical(PETSC_NULL,"-adicmf_jacobian",&adicmf_jacobian,0);CHKERRQ(ierr);
#if defined(PETSC_HAVE_ADIC) && !defined(PETSC_USE_COMPLEX) && !defined(PETSC_USE_SINGLE)
  if (adicmf_jacobian) {
    ierr = DASetLocalAdicMFFunction(user.da,admf_FormFunctionLocal);CHKERRQ(ierr);
    ierr = MatRegisterDAAD();CHKERRQ(ierr);
    ierr = MatCreateDAAD(user.da,&A);CHKERRQ(ierr);
    ierr = MatDAADSetSNES(A,snes);CHKERRQ(ierr);
    ierr = MatDAADSetCtx(A,&user);CHKERRQ(ierr);
  }    
#endif

  if (fd_jacobian) {
    ierr = DAGetColoring(user.da,IS_COLORING_LOCAL,&iscoloring);CHKERRQ(ierr);
    ierr = MatFDColoringCreate(J,iscoloring,&matfdcoloring);CHKERRQ(ierr);
    ierr = ISColoringDestroy(iscoloring);CHKERRQ(ierr);
    ierr = MatFDColoringSetFunction(matfdcoloring,(int (*)(void))FormFunction,&user);CHKERRQ(ierr);
    ierr = MatFDColoringSetFromOptions(matfdcoloring);CHKERRQ(ierr);
    ierr = SNESSetJacobian(snes,A,J,SNESDefaultComputeJacobianColor,matfdcoloring);CHKERRQ(ierr);
#if defined(PETSC_HAVE_ADIC) && !defined(PETSC_USE_COMPLEX) && !defined(PETSC_USE_SINGLE)
  } else if (adic_jacobian) {
    ierr = DAGetColoring(user.da,IS_COLORING_GHOSTED,&iscoloring);CHKERRQ(ierr);
    ierr = MatSetColoring(J,iscoloring);CHKERRQ(ierr);
    ierr = ISColoringDestroy(iscoloring);CHKERRQ(ierr);
    ierr = SNESSetJacobian(snes,A,J,SNESDAComputeJacobianWithAdic,&user);CHKERRQ(ierr);
#endif
  } else if (global_jacobian){
    ierr = SNESSetJacobian(snes,A,J,FormJacobian,&user);CHKERRQ(ierr);
  } else if (local_jacobian){
    ierr = SNESSetJacobian(snes,A,J,SNESDAComputeJacobian,&user);CHKERRQ(ierr);
  }

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Customize nonlinear solver; set runtime options
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Evaluate initial guess
     Note: The user should initialize the vector, x, with the initial guess
     for the nonlinear solver prior to calling SNESSolve().  In particular,
     to employ an initial guess of zero, the user should explicitly set
     this vector to zero by calling VecSet().
  */
  ierr = FormInitialGuess(&user,x);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Solve nonlinear system
     - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = SNESSolve(snes,x,&its);CHKERRQ(ierr); 

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Explicitly check norm of the residual of the solution
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
  ierr = FormFunction(snes,x,r,(void *)&user);CHKERRQ(ierr);
  ierr = VecNorm(r,NORM_2,&fnorm);CHKERRQ(ierr); 
  ierr = PetscPrintf(PETSC_COMM_WORLD,"Number of Newton iterations = %d fnorm %g\n",its,fnorm);CHKERRQ(ierr);

  /* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
     Free work space.  All PETSc objects should be destroyed when they
     are no longer needed.
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

  if (A != J) {
    ierr = MatDestroy(A);CHKERRQ(ierr);
  }
  ierr = MatDestroy(J);CHKERRQ(ierr);
  if (matfdcoloring) {
    ierr = MatFDColoringDestroy(matfdcoloring);CHKERRQ(ierr);
  }
  ierr = VecDestroy(x);CHKERRQ(ierr);
  ierr = VecDestroy(r);CHKERRQ(ierr);      
  ierr = SNESDestroy(snes);CHKERRQ(ierr);
  ierr = DADestroy(user.da);CHKERRQ(ierr);
  ierr = PetscFinalize();CHKERRQ(ierr);

  PetscFunctionReturn(0);
}
/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormInitialGuess"
/* 
   FormInitialGuess - Forms initial approximation.

   Input Parameters:
   user - user-defined application context
   X - vector

   Output Parameter:
   X - vector
 */
int FormInitialGuess(AppCtx *user,Vec X)
{
  int         i,j,Mx,My,ierr,xs,ys,xm,ym;
  PetscReal   lambda,temp1,temp,hx,hy;
  PetscScalar **x;

  PetscFunctionBegin;
  ierr = DAGetInfo(user->da,PETSC_IGNORE,&Mx,&My,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);

  lambda = user->param;
  hx     = 1.0/(PetscReal)(Mx-1);
  hy     = 1.0/(PetscReal)(My-1);
  temp1  = lambda/(lambda + 1.0);

  /*
     Get a pointer to vector data.
       - For default PETSc vectors, VecGetArray() returns a pointer to
         the data array.  Otherwise, the routine is implementation dependent.
       - You MUST call VecRestoreArray() when you no longer need access to
         the array.
  */
  ierr = DAVecGetArray(user->da,X,(void**)&x);CHKERRQ(ierr);

  /*
     Get local grid boundaries (for 2-dimensional DA):
       xs, ys   - starting grid indices (no ghost points)
       xm, ym   - widths of local grid (no ghost points)

  */
  ierr = DAGetCorners(user->da,&xs,&ys,PETSC_NULL,&xm,&ym,PETSC_NULL);CHKERRQ(ierr);

  /*
     Compute initial guess over the locally owned part of the grid
  */
  for (j=ys; j<ys+ym; j++) {
    temp = (PetscReal)(PetscMin(j,My-j-1))*hy;
    for (i=xs; i<xs+xm; i++) {

      if (i == 0 || j == 0 || i == Mx-1 || j == My-1) {
        /* boundary conditions are all zero Dirichlet */
        x[j][i] = 0.0; 
      } else {
        x[j][i] = temp1*sqrt(PetscMin((PetscReal)(PetscMin(i,Mx-i-1))*hx,temp)); 
      }
    }
  }

  /*
     Restore vector
  */
  ierr = DAVecRestoreArray(user->da,X,(void**)&x);CHKERRQ(ierr);

  PetscFunctionReturn(0);
} 
/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormFunction"
/* 
   FormFunction - Evaluates nonlinear function, F(x).

   Input Parameters:
.  snes - the SNES context
.  X - input vector
.  ptr - optional user-defined context, as set by SNESSetFunction()

   Output Parameter:
.  F - function vector
 */
int FormFunction(SNES snes,Vec X,Vec F,void *ptr)
{
  AppCtx       *user = (AppCtx*)ptr;
  int          ierr,i,j,Mx,My,xs,ys,xm,ym;
  PetscReal    two = 2.0,lambda,hx,hy,hxdhy,hydhx,sc;
  PetscScalar  u,uxx,uyy,**x,**f;
  Vec          localX;

  PetscFunctionBegin;
  ierr = DAGetLocalVector(user->da,&localX);CHKERRQ(ierr);
  ierr = DAGetInfo(user->da,PETSC_IGNORE,&Mx,&My,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);

  lambda = user->param;
  hx     = 1.0/(PetscReal)(Mx-1);
  hy     = 1.0/(PetscReal)(My-1);
  sc     = hx*hy*lambda;
  hxdhy  = hx/hy; 
  hydhx  = hy/hx;

  /*
     Scatter ghost points to local vector,using the 2-step process
        DAGlobalToLocalBegin(),DAGlobalToLocalEnd().
     By placing code between these two statements, computations can be
     done while messages are in transition.
  */
  ierr = DAGlobalToLocalBegin(user->da,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(user->da,X,INSERT_VALUES,localX);CHKERRQ(ierr);

  /*
     Get pointers to vector data
  */
  ierr = DAVecGetArray(user->da,localX,(void**)&x);CHKERRQ(ierr);
  ierr = DAVecGetArray(user->da,F,(void**)&f);CHKERRQ(ierr);

  /*
     Get local grid boundaries
  */
  ierr = DAGetCorners(user->da,&xs,&ys,PETSC_NULL,&xm,&ym,PETSC_NULL);CHKERRQ(ierr);

  /*
     Compute function over the locally owned part of the grid
  */
  for (j=ys; j<ys+ym; j++) {
    for (i=xs; i<xs+xm; i++) {
      if (i == 0 || j == 0 || i == Mx-1 || j == My-1) {
        f[j][i] = x[j][i];
      } else {
        u       = x[j][i];
        uxx     = (two*u - x[j][i-1] - x[j][i+1])*hydhx;
        uyy     = (two*u - x[j-1][i] - x[j+1][i])*hxdhy;
        f[j][i] = uxx + uyy - sc*PetscExpScalar(u);
      }
    }
  }

  /*
     Restore vectors
  */
  ierr = DAVecRestoreArray(user->da,localX,(void**)&x);CHKERRQ(ierr);
  ierr = DAVecRestoreArray(user->da,F,(void**)&f);CHKERRQ(ierr);
  ierr = DARestoreLocalVector(user->da,&localX);CHKERRQ(ierr);
  ierr = PetscLogFlops(11*ym*xm);CHKERRQ(ierr);
  PetscFunctionReturn(0); 
} 
/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormFunctionLocal"
/* 
   FormFunctionLocal - Evaluates nonlinear function, F(x).

       Process adiC: FormFunctionLocal

 */
int FormFunctionLocal(DALocalInfo *info,PetscScalar **x,PetscScalar **f,AppCtx *user)
{
  int         ierr,i,j;
  PetscReal   two = 2.0,lambda,hx,hy,hxdhy,hydhx,sc;
  PetscScalar u,uxx,uyy;

  PetscFunctionBegin;

  lambda = user->param;
  hx     = 1.0/(PetscReal)(info->mx-1);
  hy     = 1.0/(PetscReal)(info->my-1);
  sc     = hx*hy*lambda;
  hxdhy  = hx/hy; 
  hydhx  = hy/hx;
  /*
     Compute function over the locally owned part of the grid
  */
  for (j=info->ys; j<info->ys+info->ym; j++) {
    for (i=info->xs; i<info->xs+info->xm; i++) {
      if (i == 0 || j == 0 || i == info->mx-1 || j == info->my-1) {
        f[j][i] = x[j][i];
      } else {
        u       = x[j][i];
        uxx     = (two*u - x[j][i-1] - x[j][i+1])*hydhx;
        uyy     = (two*u - x[j-1][i] - x[j+1][i])*hxdhy;
        f[j][i] = uxx + uyy - sc*PetscExpScalar(u);
      }
    }
  }

  ierr = PetscLogFlops(11*info->ym*info->xm);CHKERRQ(ierr);
  PetscFunctionReturn(0); 
} 
/* ------------------------------------------------------------------- */
#undef __FUNCT__
#define __FUNCT__ "FormJacobian"
/*
   FormJacobian - Evaluates Jacobian matrix.

   Input Parameters:
.  snes - the SNES context
.  x - input vector
.  ptr - optional user-defined context, as set by SNESSetJacobian()

   Output Parameters:
.  A - Jacobian matrix
.  B - optionally different preconditioning matrix
.  flag - flag indicating matrix structure

*/
int FormJacobian(SNES snes,Vec X,Mat *J,Mat *B,MatStructure *flag,void *ptr)
{
  AppCtx       *user = (AppCtx*)ptr;  /* user-defined application context */
  Mat          jac = *B;                /* Jacobian matrix */
  Vec          localX;
  int          ierr,i,j;
  MatStencil   col[5],row;
  int          xs,ys,xm,ym,Mx,My;
  PetscScalar  lambda,v[5],hx,hy,hxdhy,hydhx,sc,**x;

  PetscFunctionBegin;
  ierr = DAGetLocalVector(user->da,&localX);CHKERRQ(ierr);
  ierr = DAGetInfo(user->da,PETSC_IGNORE,&Mx,&My,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);

  lambda = user->param;
  hx     = 1.0/(PetscReal)(Mx-1);
  hy     = 1.0/(PetscReal)(My-1);
  sc     = hx*hy*lambda;
  hxdhy  = hx/hy; 
  hydhx  = hy/hx;


  /*
     Scatter ghost points to local vector, using the 2-step process
        DAGlobalToLocalBegin(), DAGlobalToLocalEnd().
     By placing code between these two statements, computations can be
     done while messages are in transition.
  */
  ierr = DAGlobalToLocalBegin(user->da,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(user->da,X,INSERT_VALUES,localX);CHKERRQ(ierr);

  /*
     Get pointer to vector data
  */
  ierr = DAVecGetArray(user->da,localX,(void**)&x);CHKERRQ(ierr);

  /*
     Get local grid boundaries
  */
  ierr = DAGetCorners(user->da,&xs,&ys,PETSC_NULL,&xm,&ym,PETSC_NULL);CHKERRQ(ierr);

  /* 
     Compute entries for the locally owned part of the Jacobian.
      - Currently, all PETSc parallel matrix formats are partitioned by
        contiguous chunks of rows across the processors. 
      - Each processor needs to insert only elements that it owns
        locally (but any non-local elements will be sent to the
        appropriate processor during matrix assembly). 
      - Here, we set all entries for a particular row at once.
      - We can set matrix entries either using either
        MatSetValuesLocal() or MatSetValues(), as discussed above.
  */
  for (j=ys; j<ys+ym; j++) {
    for (i=xs; i<xs+xm; i++) {
      row.j = j; row.i = i;
      /* boundary points */
      if (i == 0 || j == 0 || i == Mx-1 || j == My-1) {
        v[0] = 1.0;
        ierr = MatSetValuesStencil(jac,1,&row,1,&row,v,INSERT_VALUES);CHKERRQ(ierr);
      } else {
      /* interior grid points */
        v[0] = -hxdhy;                                           col[0].j = j - 1; col[0].i = i;
        v[1] = -hydhx;                                           col[1].j = j;     col[1].i = i-1;
        v[2] = 2.0*(hydhx + hxdhy) - sc*PetscExpScalar(x[j][i]); col[2].j = row.j; col[2].i = row.i;
        v[3] = -hydhx;                                           col[3].j = j;     col[3].i = i+1;
  
      v[4] = -hxdhy;                                           col[4].j = j + 1; col[4].i = i;
        ierr = MatSetValuesStencil(jac,1,&row,5,col,v,INSERT_VALUES);CHKERRQ(ierr);
      }
    }
  }
  ierr = DAVecRestoreArray(user->da,localX,(void**)&x);CHKERRQ(ierr);
  ierr = DARestoreLocalVector(user->da,&localX);CHKERRQ(ierr);

  /* 
     Assemble matrix, using the 2-step process:
       MatAssemblyBegin(), MatAssemblyEnd().
  */
  ierr = MatAssemblyBegin(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  /*
     Normally since the matrix has already been assembled above; this
     would do nothing. But in the matrix free mode -snes_mf_operator
     this tells the "matrix-free" matrix that a new linear system solve
     is about to be done.
  */

  ierr = MatAssemblyBegin(*J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(*J,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

  /*
     Set flag to indicate that the Jacobian matrix retains an identical
     nonzero structure throughout all nonlinear iterations (although the
     values of the entries change). Thus, we can save some work in setting
     up the preconditioner (e.g., no need to redo symbolic factorization for
     ILU/ICC preconditioners).
      - If the nonzero structure of the matrix is different during
        successive linear solves, then the flag DIFFERENT_NONZERO_PATTERN
        must be used instead.  If you are unsure whether the matrix
        structure has changed or not, use the flag DIFFERENT_NONZERO_PATTERN.
      - Caution:  If you specify SAME_NONZERO_PATTERN, PETSc
        believes your assertion and does not check the structure
        of the matrix.  If you erroneously claim that the structure
        is the same when it actually is not, the new preconditioner
        will not function correctly.  Thus, use this optimization
        feature with caution!
  */
  *flag = SAME_NONZERO_PATTERN;


  /*
     Tell the matrix we will never add a new nonzero location to the
     matrix. If we do, it will generate an error.
  */
  ierr = MatSetOption(jac,MAT_NEW_NONZERO_LOCATION_ERR);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "FormJacobianLocal"
/*
   FormJacobianLocal - Evaluates Jacobian matrix.


*/
int FormJacobianLocal(DALocalInfo *info,PetscScalar **x,Mat jac,AppCtx *user)
{
  int          ierr,i,j;
  MatStencil   col[5],row;
  PetscScalar  lambda,v[5],hx,hy,hxdhy,hydhx,sc;

  PetscFunctionBegin;
  lambda = user->param;
  hx     = 1.0/(PetscReal)(info->mx-1);
  hy     = 1.0/(PetscReal)(info->my-1);
  sc     = hx*hy*lambda;
  hxdhy  = hx/hy; 
  hydhx  = hy/hx;


  /* 
     Compute entries for the locally owned part of the Jacobian.
      - Currently, all PETSc parallel matrix formats are partitioned by
        contiguous chunks of rows across the processors. 
      - Each processor needs to insert only elements that it owns
        locally (but any non-local elements will be sent to the
        appropriate processor during matrix assembly). 
      - Here, we set all entries for a particular row at once.
      - We can set matrix entries either using either
        MatSetValuesLocal() or MatSetValues(), as discussed above.
  */
  for (j=info->ys; j<info->ys+info->ym; j++) {
    for (i=info->xs; i<info->xs+info->xm; i++) {
      row.j = j; row.i = i;
      /* boundary points */
      if (i == 0 || j == 0 || i == info->mx-1 || j == info->my-1) {
        v[0] = 1.0;
        ierr = MatSetValuesStencil(jac,1,&row,1,&row,v,INSERT_VALUES);CHKERRQ(ierr);
      } else {
      /* interior grid points */
        v[0] = -hxdhy;                                           col[0].j = j - 1; col[0].i = i;
        v[1] = -hydhx;                                           col[1].j = j;     col[1].i = i-1;
        v[2] = 2.0*(hydhx + hxdhy) - sc*PetscExpScalar(x[j][i]); col[2].j = row.j; col[2].i = row.i;
        v[3] = -hydhx;                                           col[3].j = j;     col[3].i = i+1;
        v[4] = -hxdhy;                                           col[4].j = j + 1; col[4].i = i;
        ierr = MatSetValuesStencil(jac,1,&row,5,col,v,INSERT_VALUES);CHKERRQ(ierr);
      }
    }
  }

  /* 
     Assemble matrix, using the 2-step process:
       MatAssemblyBegin(), MatAssemblyEnd().
  */
  ierr = MatAssemblyBegin(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  ierr = MatAssemblyEnd(jac,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
  /*
     Tell the matrix we will never add a new nonzero location to the
     matrix. If we do, it will generate an error.
  */
  ierr = MatSetOption(jac,MAT_NEW_NONZERO_LOCATION_ERR);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*
      Variant of FormFunction() that computes the function in Matlab
*/
#if defined(PETSC_HAVE_MATLAB_ENGINE) && !defined(PETSC_USE_COMPLEX) && !defined(PETSC_USE_SINGLE)
int FormFunctionMatlab(SNES snes,Vec X,Vec F,void *ptr)
{
  AppCtx    *user = (AppCtx*)ptr;
  int       ierr,Mx,My;
  PetscReal lambda,hx,hy;
  Vec       localX,localF;
  MPI_Comm  comm;

  PetscFunctionBegin;
  ierr = DAGetLocalVector(user->da,&localX);CHKERRQ(ierr);
  ierr = DAGetLocalVector(user->da,&localF);CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)localX,"localX");CHKERRQ(ierr);
  ierr = PetscObjectSetName((PetscObject)localF,"localF");CHKERRQ(ierr);
  ierr = DAGetInfo(user->da,PETSC_IGNORE,&Mx,&My,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,
                   PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE,PETSC_IGNORE);

  lambda = user->param;
  hx     = 1.0/(PetscReal)(Mx-1);
  hy     = 1.0/(PetscReal)(My-1);

  ierr = PetscObjectGetComm((PetscObject)snes,&comm);CHKERRQ(ierr);
  /*
     Scatter ghost points to local vector,using the 2-step process
        DAGlobalToLocalBegin(),DAGlobalToLocalEnd().
     By placing code between these two statements, computations can be
     done while messages are in transition.
  */
  ierr = DAGlobalToLocalBegin(user->da,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = DAGlobalToLocalEnd(user->da,X,INSERT_VALUES,localX);CHKERRQ(ierr);
  ierr = PetscMatlabEnginePut(PETSC_MATLAB_ENGINE_(comm),(PetscObject)localX);CHKERRQ(ierr);
  ierr = PetscMatlabEngineEvaluate(PETSC_MATLAB_ENGINE_(comm),"localF=ex5m(localX,%18.16e,%18.16e,%18.16e)",hx,hy,lambda);CHKERRQ(ierr);
  ierr = PetscMatlabEngineGet(PETSC_MATLAB_ENGINE_(comm),(PetscObject)localF);CHKERRQ(ierr);

  /*
     Insert values into global vector
  */
  ierr = DALocalToGlobal(user->da,localF,INSERT_VALUES,F);CHKERRQ(ierr);
  ierr = DARestoreLocalVector(user->da,&localX);CHKERRQ(ierr);
  ierr = DARestoreLocalVector(user->da,&localF);CHKERRQ(ierr);
  PetscFunctionReturn(0); 
} 
#endif










