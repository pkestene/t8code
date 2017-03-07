/*
  This file is part of t8code.
  t8code is a C library to manage a collection (a forest) of multiple
  connected adaptive space-trees of general element types in parallel.

  Copyright (C) 2015 the developers

  t8code is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  t8code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with t8code; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include <sc_refcount.h>
#include <t8_default.h>
#include <t8_forest/t8_forest_adapt.h>
#include <t8_forest.h>
#include <t8_cmesh_vtk.h>
#include <p4est_connectivity.h>
#include <p8est_connectivity.h>
#include <sc_shmem.h>

#if 1
static int
t8_basic_adapt (t8_forest_t forest, t8_locidx_t which_tree,
                t8_eclass_scheme_t * ts,
                int num_elements, t8_element_t * elements[])
{
  int                 level;
  T8_ASSERT (num_elements == 1 || num_elements ==
             t8_eclass_num_children[ts->eclass]);
  level = t8_element_level (ts, elements[0]);
#if 0
  if (num_elements > 1) {
    /* Do coarsen here */
    if (level > 0)
      return -1;
    return 0;
  }
#endif
  if (level < 4)
    /* refine randomly if level is smaller 3 */
    return rand () % 2;
  return 0;
}

static int
t8_basic_adapt_cse (t8_forest_t forest, t8_locidx_t which_tree,
                    t8_eclass_scheme_t * ts,
                    int num_elements, t8_element_t * elements[])
{
  int                 level;
  t8_gloidx_t         glo_tree;
  t8_gloidx_t         refine_this_tree;

  T8_ASSERT (num_elements == 1 || num_elements ==
             t8_eclass_num_children[ts->eclass]);

  /* refine a particular tree that was given as user data */
  refine_this_tree = *(t8_gloidx_t *) t8_forest_get_user_data (forest);
  /* The level of the element */
  level = t8_element_level (ts, elements[0]);
  /* Get the global id of the current tree */
  glo_tree = t8_cmesh_get_global_id (t8_forest_get_cmesh (forest),
                                     t8_forest_ltreeid_to_cmesh_ltreeid
                                     (forest, which_tree));

  if (num_elements > 1 && glo_tree != refine_this_tree) {
    /* Do coarsen here */
    if (level > 2)
      return -1;
  }
  /* We refine one tree more than the others, since we want it to be shared by
   * some procs */
  if (glo_tree == refine_this_tree) {
    t8_locidx_t         anchor[3];
    int8_t              maxlevel = t8_element_maxlevel (ts);
    t8_element_vertex_coords (ts, elements[0], 1, anchor);
    if (anchor[0] > (1 << (maxlevel - level + 1))
        && anchor[0] < t8_element_root_len (ts, elements[0])) {
      return level < 4;
    }
    else
      return level < 3;
  }
  else {
    /* The rest has max level 2 */
    if (level < 2)
      return 1;
  }
  return 0;
}
#endif

#if 1
static void
t8_basic_refine_test ()
{
  t8_forest_t         forest;
  t8_forest_t         forest_adapt;
  t8_cmesh_t          cmesh;

  t8_forest_init (&forest);
  t8_forest_init (&forest_adapt);
  cmesh = t8_cmesh_new_hypercube (T8_ECLASS_HEX, sc_MPI_COMM_WORLD, 0, 0);

  t8_forest_set_cmesh (forest, cmesh, sc_MPI_COMM_WORLD);
  t8_cmesh_unref (&cmesh);
  t8_forest_set_scheme (forest, t8_scheme_new_default ());
  t8_forest_set_level (forest, 2);
  t8_forest_commit (forest);

  t8_forest_set_adapt (forest_adapt, forest, t8_basic_adapt, NULL, 1);
  t8_forest_commit (forest_adapt);
  t8_forest_write_vtk (forest_adapt, "forest_basic_refine");

  t8_forest_unref (&forest_adapt);
}
#endif

/* Generate a 1xn brick connectivity, create a forest with nontrivial
 * refinement on it and print to vtk.
 * We used this function to create plots for our talk at
 * the SIAM CSE 2017 */
static void
t8_basic_for_cse_talk (int m_x)
{
  int                 level = 1;
  t8_cmesh_t          cmesh, cmesh_partition;
  t8_forest_t         forest, forest_adapt, forest_part;
  p4est_connectivity_t *conn;
  t8_gloidx_t         refine_this_tree;

  /* Create m_x x 1 brick conn */
  conn = p4est_connectivity_new_brick (m_x, 1, 0, 0);
  /* build partitioned cmesh from connectivity */
  cmesh = t8_cmesh_new_from_p4est (conn, sc_MPI_COMM_WORLD, 1);
  p4est_connectivity_destroy (conn);
  /* partition the cmesh according to initial uniform forest */
  t8_cmesh_init (&cmesh_partition);
  t8_cmesh_set_derive (cmesh_partition, cmesh);
  t8_cmesh_set_partition_uniform (cmesh_partition, level);
  t8_cmesh_commit (cmesh_partition, sc_MPI_COMM_WORLD);

  /* Create a uniform forest */
  t8_forest_init (&forest);
  t8_forest_set_cmesh (forest, cmesh_partition, sc_MPI_COMM_WORLD);
  t8_cmesh_unref (&cmesh_partition);
  t8_cmesh_unref (&cmesh);
  t8_forest_set_scheme (forest, t8_scheme_new_default ());
  t8_forest_set_level (forest, level);
  t8_forest_commit (forest);
  t8_debugf ("[H] Created initial forest\n");

  /* adapt the forest */
  t8_forest_ref (forest);       /* We want to adapt it another time below and thus reference it here */
  t8_forest_init (&forest_adapt);
  refine_this_tree = 1;
  t8_forest_set_user_data (forest_adapt, (void *) &refine_this_tree);
  t8_forest_set_adapt (forest_adapt, forest, t8_basic_adapt_cse, NULL, 1);
  t8_forest_commit (forest_adapt);

  /* partition the coarse mesh according to the new forest partition */
  t8_forest_partition_cmesh (forest_adapt, sc_MPI_COMM_WORLD, 0);

  t8_debugf ("[H] Created adapted forest\n");
  t8_forest_write_vtk (forest_adapt, "cse_forest_adapt");
  /* partition forest */
  t8_forest_init (&forest_part);
  t8_forest_set_partition (forest_part, forest_adapt, 0);
  t8_forest_commit (forest_part);

  t8_debugf ("[H] Created partitioned forest\n");
  /* partition the coarse mesh according to the new forest partition */
  t8_forest_partition_cmesh (forest_part, sc_MPI_COMM_WORLD, 0);
  t8_debugf ("[H] Partitioned coarse mesh\n");
  t8_forest_write_vtk (forest_part, "cse_forest_part");
  t8_cmesh_vtk_write_file (t8_forest_get_cmesh (forest_part),
                           "cse_cmesh_part", 1.0);

  /* Re-adapt the forest */
  t8_forest_init (&forest_adapt);
  refine_this_tree = 2;
  t8_forest_set_user_data (forest_adapt, (void *) &refine_this_tree);
  t8_forest_set_adapt (forest_adapt, forest, t8_basic_adapt_cse, NULL, 1);
  t8_forest_commit (forest_adapt);

  t8_debugf ("[H] Created adapted forest 2\n");
  t8_forest_write_vtk (forest_adapt, "cse_forest_adapt2");
  /* Re-partition the forest */
  t8_forest_unref (&forest_part);

  t8_forest_init (&forest_part);
  t8_forest_set_partition (forest_part, forest_adapt, 0);
  t8_forest_commit (forest_part);

  t8_debugf ("[H] Created partitioned forest\n");
  /* partition the coarse mesh according to the new forest partition */
  t8_forest_partition_cmesh (forest_part, sc_MPI_COMM_WORLD, 0);
  t8_debugf ("[H] Partitioned coarse mesh\n");
  t8_forest_write_vtk (forest_part, "cse_forest_part2");
  t8_cmesh_vtk_write_file (t8_forest_get_cmesh (forest_part),
                           "cse_cmesh_part2", 1.0);

  t8_forest_unref (&forest_part);
}

#if 0
static void
t8_basic_forest_partition ()
{
  t8_forest_t         forest, forest_adapt, forest_partition;
  t8_cmesh_t          cmesh, cmesh_partition;
  sc_MPI_Comm         comm;
  int                 level = 2;        /* initial refinement level */

  comm = sc_MPI_COMM_WORLD;
  cmesh = t8_cmesh_new_hypercube (T8_ECLASS_QUAD, comm, 0, 1);
  t8_cmesh_init (&cmesh_partition);
  t8_cmesh_set_derive (cmesh_partition, cmesh);
  t8_cmesh_set_partition_uniform (cmesh_partition, level);
  t8_cmesh_commit (cmesh_partition, comm);
  t8_cmesh_unref (&cmesh);
  t8_forest_init (&forest);
  t8_forest_init (&forest_adapt);
  t8_forest_init (&forest_partition);
  t8_forest_set_cmesh (forest, cmesh_partition, comm);
  t8_forest_set_scheme (forest, t8_scheme_new_default ());
  t8_forest_set_level (forest, level);
  t8_forest_commit (forest);
  t8_forest_set_adapt (forest_adapt, forest, t8_basic_adapt, NULL, 1);
  t8_forest_set_partition (forest_partition, forest_adapt, 0);
  t8_forest_commit (forest_adapt);
  t8_forest_commit (forest_partition);
  t8_forest_partition_cmesh (forest_partition, comm, 0);

  /* Clean-up */
  t8_cmesh_destroy (&cmesh_partition);
  t8_forest_unref (&forest_partition);
}
#endif

#if 1
static void
t8_basic_hypercube (t8_eclass_t eclass, int set_level,
                    int create_forest, int do_partition)
{
  t8_forest_t         forest;
  t8_cmesh_t          cmesh;
  char                vtuname[BUFSIZ];
  int                 mpirank, mpiret;

  t8_global_productionf ("Entering t8_basic hypercube %s\n",
                         t8_eclass_to_string[eclass]);

  cmesh = t8_cmesh_new_hypercube (eclass, sc_MPI_COMM_WORLD, 0, do_partition);

  mpiret = sc_MPI_Comm_rank (sc_MPI_COMM_WORLD, &mpirank);
  SC_CHECK_MPI (mpiret);

  snprintf (vtuname, BUFSIZ, "cmesh_hypercube_%s",
            t8_eclass_to_string[eclass]);
  if (t8_cmesh_vtk_write_file (cmesh, vtuname, 1.0) == 0) {
    t8_debugf ("Output to %s\n", vtuname);
  }
  else {
    t8_debugf ("Error in output\n");
  }
  if (create_forest) {
    t8_forest_init (&forest);
    t8_forest_set_cmesh (forest, cmesh, sc_MPI_COMM_WORLD);
    t8_forest_set_scheme (forest, t8_scheme_new_default ());

    t8_forest_set_level (forest, set_level);

    if (eclass == T8_ECLASS_QUAD || eclass == T8_ECLASS_HEX
        || eclass == T8_ECLASS_TRIANGLE || eclass == T8_ECLASS_TET) {
      t8_forest_commit (forest);
      t8_debugf ("Successfully committed forest.\n");
      t8_forest_write_vtk (forest, "forest_basic");     /* This does nothing right now */
      t8_forest_unref (&forest);
    }
  }
  t8_cmesh_destroy (&cmesh);
}
#endif

#if 0
static void
t8_basic_periodic (int set_level, int dim)
{
  t8_forest_t         forest;

  t8_forest_init (&forest);

  t8_forest_set_cmesh (forest, t8_cmesh_new_periodic (sc_MPI_COMM_WORLD,
                                                      dim));
  t8_forest_set_scheme (forest, t8_scheme_new_default ());

  t8_forest_set_level (forest, set_level);

  t8_forest_unref (&forest);
}
#endif

#if 0
static void
t8_basic_p4est (int do_partition, int create_forest, int forest_level)
{
  t8_cmesh_t          cmesh;
  t8_forest_t         forest;
  p4est_connectivity_t *conn;

  conn = p4est_connectivity_new_moebius ();
  cmesh = t8_cmesh_new_from_p4est (conn, sc_MPI_COMM_WORLD, 0, do_partition);
  p4est_connectivity_destroy (conn);
  t8_cmesh_vtk_write_file (cmesh, "t8_p4est_moebius", 1.);
  if (create_forest) {
    /* To make shure that the cmesh has each tree that the forest
     * needs, even if forest_level > 0, we create a new cmesh that
     * is partitioned according to uniform level refinement. */
    t8_cmesh_t          cmesh_new;
    t8_cmesh_init (&cmesh_new);
    t8_cmesh_set_derive (cmesh_new, cmesh);
    t8_cmesh_set_partition_uniform (cmesh_new, forest_level);
    t8_cmesh_commit (cmesh_new, sc_MPI_COMM_WORLD);
    t8_forest_init (&forest);
    t8_forest_set_scheme (forest, t8_scheme_new_default ());
    t8_forest_set_cmesh (forest, cmesh_new, sc_MPI_COMM_WORLD);
    t8_forest_set_level (forest, forest_level);
    t8_forest_commit (forest);
    t8_debugf ("Successfully committed forest.\n");
    t8_forest_unref (&forest);
    t8_cmesh_destroy (&cmesh_new);
  }
  t8_cmesh_unref (&cmesh);
}
#endif

#if 0
static void
t8_basic_p8est (int x, int y, int z)
{
  t8_cmesh_t          cmesh;
  p8est_connectivity_t *conn;

  conn = p8est_connectivity_new_brick (x, y, z, 0, 0, 0);
  cmesh = t8_cmesh_new_from_p8est (conn, sc_MPI_COMM_WORLD, 0);
  p8est_connectivity_destroy (conn);
  t8_cmesh_vtk_write_file (cmesh, "t8_p8est_brick", 1.);
  t8_cmesh_unref (&cmesh);
#ifdef T8_WITH_METIS
  {
    int                 mpirank, mpiret;
    mpiret = sc_MPI_Comm_rank (sc_MPI_COMM_WORLD, &mpirank);
    SC_CHECK_MPI (mpiret);
    p8est_connectivity_reorder (sc_MPI_COMM_WORLD,
                                mpirank, conn, P8EST_CONNECT_FULL);
    cmesh = t8_cmesh_new_from_p8est (conn, sc_MPI_COMM_WORLD, 0);
    t8_cmesh_vtk_write_file (cmesh, "t8_p8est_brick_metis", 1.);
  }
#endif
  t8_cmesh_unref (&cmesh);
  p8est_connectivity_destroy (conn);
}

static void
t8_basic_partitioned ()
{
  t8_cmesh_t          cmesh;
  int                 mpirank, mpisize, mpiret;
  int                 first_tree, last_tree, i;
  const int           num_trees = 11;

  mpiret = sc_MPI_Comm_rank (sc_MPI_COMM_WORLD, &mpirank);
  SC_CHECK_MPI (mpiret);
  mpiret = sc_MPI_Comm_size (sc_MPI_COMM_WORLD, &mpisize);
  SC_CHECK_MPI (mpiret);
  first_tree = (mpirank * num_trees) / mpisize;
  last_tree = ((mpirank + 1) * num_trees) / mpisize - 1;
  t8_debugf ("Range in %i trees: [%i,%i]\n", num_trees, first_tree,
             last_tree);
  t8_cmesh_init (&cmesh);
  if (cmesh == NULL) {
    return;
  }
  t8_cmesh_set_partition_range (cmesh, 3, first_tree, last_tree);
  for (i = first_tree > 1 ? first_tree : 2; i <= last_tree; i++) {
    t8_cmesh_set_tree_class (cmesh, i, T8_ECLASS_TRIANGLE);
  }
  t8_cmesh_set_tree_class (cmesh, 0, T8_ECLASS_TRIANGLE);
  t8_cmesh_set_tree_class (cmesh, 1, T8_ECLASS_TRIANGLE);
  t8_cmesh_set_join (cmesh, 0, 1, 0, 0, 0);
  t8_cmesh_commit (cmesh, sc_MPI_COMM_WORLD);
  t8_cmesh_unref (&cmesh);
}
#endif
#if 0
static void
t8_basic ()
{
  t8_cmesh_t          cmesh, cmesh_partition;

  cmesh = t8_cmesh_new_bigmesh (T8_ECLASS_TET, 190, sc_MPI_COMM_WORLD);
  t8_cmesh_init (&cmesh_partition);
  t8_cmesh_set_derive (cmesh_partition, cmesh);
  t8_cmesh_unref (&cmesh);
  t8_cmesh_set_partition_uniform (cmesh_partition, 1);
  t8_cmesh_commit (cmesh_partition, sc_MPI_COMM_WORLD);
  t8_cmesh_destroy (&cmesh_partition);
}

#endif
#if 0
static void
t8_basic_partition (t8_eclass_t eclass, int set_level)
{
  t8_cmesh_t          cmesh, cmesh_part;
  char                file[BUFSIZ];
  int                 mpirank, mpiret, mpisize, iproc;
  t8_gloidx_t        *offsets;

  mpiret = sc_MPI_Comm_rank (sc_MPI_COMM_WORLD, &mpirank);
  SC_CHECK_MPI (mpiret);
  mpiret = sc_MPI_Comm_size (sc_MPI_COMM_WORLD, &mpisize);
  SC_CHECK_MPI (mpiret);

  offsets = SC_SHMEM_ALLOC (t8_gloidx_t, mpisize + 1, sc_MPI_COMM_WORLD);

  t8_cmesh_init (&cmesh_part);
  cmesh = t8_cmesh_new_hypercube (eclass, sc_MPI_COMM_WORLD, 0, 0, 1);
  snprintf (file, BUFSIZ, "basic_before_partition");
  t8_cmesh_vtk_write_file (cmesh, file, 1.0);
  /* A partition that concentrates everything to proc 0 */
  offsets[0] = 0;
  offsets[1] = t8_cmesh_get_num_trees (cmesh) / 2;
  offsets[2] = t8_cmesh_get_num_trees (cmesh);
  for (iproc = 3; iproc <= mpisize; iproc++) {
    offsets[iproc] = offsets[2];
  }
  //SC_SHMEM_FREE (offsets, sc_MPI_COMM_WORLD);
  t8_cmesh_set_derive (cmesh_part, cmesh);
  /* TODO: indicate/document how first and last local tree can be left open,
   *       same idea for face_knowledge */
  t8_cmesh_set_partition_offsets (cmesh_part, offsets);
  t8_cmesh_commit (cmesh_part, sc_MPI_COMM_WORLD);
  snprintf (file, BUFSIZ, "basic_partition");
  t8_cmesh_vtk_write_file (cmesh_part, file, 1.0);
  t8_cmesh_unref (&cmesh_part);
}
#endif
int
main (int argc, char **argv)
{
  int                 mpiret;

#if 0
  int                 level = 0;
  int                 eclass;
  int                 dim;
#endif

  mpiret = sc_MPI_Init (&argc, &argv);
  SC_CHECK_MPI (mpiret);

  sc_init (sc_MPI_COMM_WORLD, 1, 1, NULL, SC_LP_ESSENTIAL);
  t8_init (SC_LP_DEFAULT);

  t8_global_productionf ("Testing basic tet mesh.\n");

#if 0
  t8_basic_partitioned ();
#endif

#if 0
  t8_basic (0, level);
  t8_basic (1, level);
  t8_basic (0, level);
  t8_basic (1, level);
  t8_global_productionf ("Done testing basic tet mesh.\n");
  t8_basic_hypercube (T8_ECLASS_QUAD, 0, 1, 1);
  t8_basic ();
#endif
  t8_basic_for_cse_talk (3);

  t8_basic_hypercube (T8_ECLASS_QUAD, 2, 1, 0);
  t8_basic_refine_test ();
  t8_global_productionf ("Testing hypercube cmesh.\n");
#if 0
  for (eclass = T8_ECLASS_QUAD; eclass < T8_ECLASS_COUNT; eclass++) {
    /* Construct the mesh on each process */
    t8_basic_hypercube ((t8_eclass_t) eclass, level, 0, 1);
    t8_basic_hypercube ((t8_eclass_t) eclass, level, 0, 1);
    t8_basic_hypercube ((t8_eclass_t) eclass, level, 1, 1);
    t8_basic_hypercube ((t8_eclass_t) eclass, level, 1, 1);
    /* Construct the mesh on one process and broadcast it */
#if 0
    t8_basic_hypercube ((t8_eclass_t) eclass, 0, level, 0, 1, 0);
    t8_basic_hypercube ((t8_eclass_t) eclass, 1, level, 0, 1, 0);
    t8_basic_hypercube ((t8_eclass_t) eclass, 0, level, 1, 1, 0);
    t8_basic_hypercube ((t8_eclass_t) eclass, 1, level, 1, 1, 0);
#endif
  }
#endif
#if 0
  t8_global_productionf ("Done testing hypercube cmesh.\n");

  t8_global_productionf ("Testing periodic cmesh.\n");
  for (dim = 1; dim < 4; dim++) {
    t8_basic_periodic (0, level, dim);
    t8_basic_periodic (1, level, dim);
  }
  t8_global_productionf ("Done testing periodic cmesh.\n");

  t8_global_productionf ("Testing adapt forest.\n");
  t8_basic_refine_test ();
  t8_global_productionf ("Done testing adapt forest.\n");

  t8_global_productionf ("Testing cmesh from p4est.\n");
  t8_basic_p4est (0);
  t8_basic_p4est (1);
  t8_global_productionf ("Done testing cmesh from p4est.\n");
#if 1
  t8_global_productionf ("Testing cmesh from p8est.\n");
  t8_basic_p8est (0, 10, 13, 17);
  t8_basic_p8est (1, 10, 13, 17);
  t8_global_productionf ("Done testing cmesh from p8est.\n");
#endif
#endif

  sc_finalize ();

  mpiret = sc_MPI_Finalize ();
  SC_CHECK_MPI (mpiret);

  return 0;
}
