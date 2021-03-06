
#include <petscsys.h>
#include <petsctime.h>

int main(int argc,char **argv)
{
  PetscLogDouble x,y,z;
  PetscScalar    A[10000];
  PetscErrorCode ierr;

  ierr = PetscInitialize(&argc,&argv,0,0);if (ierr) return ierr;
  /* To take care of paging effects */
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscTime(&x);CHKERRQ(ierr);

  ierr = PetscTime(&x);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*10000);CHKERRQ(ierr);
  ierr = PetscTime(&y);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscMemzero(A,sizeof(PetscScalar)*0);CHKERRQ(ierr);
  ierr = PetscTime(&z);CHKERRQ(ierr);

  fprintf(stdout,"%s : \n","PetscMemzero");
  fprintf(stdout,"    %-15s : %e sec\n","Latency",(z-y)/10.0);
  fprintf(stdout,"    %-15s : %e sec\n","Per PetscScalar",(2*y-x-z)/100000.0);

  ierr = PetscFinalize();
  return ierr;
}
