#include "mpi.h"

#include "utils.hpp"
#include "vector-add-serial.hpp"

MPI_Comm comm;
int comm_rank;
int comm_size;

void vector_add() {
  using T = int;

  // size of distributed vector
  const size_t n = 5 * comm_size;

  // Compute the reference data
  vector_add_serial<T> ref_adder;
  if (comm_rank == 0) {
    ref_adder.init(n);
    ref_adder.compute();
  }

  // Block cyclic distribution takes a block size and
  // an optional team object (e.g. MPI communicator).

  // `lib::div()` is a special constant that indicates
  // a block size that evenly divides the vector amongst
  // all procs.
  // lib::block_cyclic(1) - true cyclic
  // lib::block_cyclic(2) - cyclic with blocks of two elements
  // lib::block_cyclic(8) - cyclic with blocks of eight elements
  // etc.
  auto dist = lib::block_cyclic(lib::div(), comm);
  lib::distributed_vector<T> a(n, dist), b(n, dist), c(n, dist);

  // Distribute the data
  a = ref_adder.a;
  b = ref_adder.b;

  // This is ok for 1 rank/core.
  // What if I want to use 1 rank/node and openmp within the node?
  // Need foreach slice and then openmp loop nested inside.
  //
  // #pragma omp parallel for
  //
  // Ben: in my view, `lib::for_each` should automatically handle threading
  //      through one of its execution policies (probably by default).
  //      In general, C++ does the correct thing---lib::foreach() can call
  //      `std::for_each()` with the `par_unseq` policy on the local ranges and
  //      it will run in parallel if it has the cores/hyperthreads allocated.
  lib::for_each(lib::parallel_explicit(), std::ranges::iota_view<size_t>(0, n),
                [&](size_t i) { c[i] = a[i] + b[i]; });

  // Collect the results
  std::vector<T> result(n);
  // No way to say that I only need the result on root?
  // Ben: We should probably be able to express this with the following.
  //      Whether this is a collective call or not I'm not 100% sure.
  // lib::copy(c.begin(), c.end(), result.begin());
  result = c;

  // Check
  if (comm_rank == 0) {
    show("a: ", ref_adder.a);
    show("b: ", ref_adder.b);
    show("c: ", c_full);
    ref_adder.check(result);
  }
}

int main(int argc, char *argv[]) {
  MPI_Init(&argc, &argv);
  comm = MPI_COMM_WORLD;
  MPI_Comm_rank(comm, &comm_rank);
  MPI_Comm_size(comm, &comm_size);

  vector_add();

  MPI_Finalize();
  return 0;
}