//===------------------------------------------------------------*- C++ -*-===//
//
// Copyright 2018 Battelle Memorial Institute
//
//===----------------------------------------------------------------------===//

#ifndef IM_MPI_IMM_H
#define IM_MPI_IMM_H

#include "mpi.h"

#include <utility>
#include <vector>
#include <cstddef>

#include "trng/lcg64.hpp"

#include "im/generate_rrr_sets.h"
#include "im/imm.h"
#include "im/mpi/find_most_influential.h"
#include "im/utility.h"

namespace im {

inline size_t ThetaPrime(ssize_t x, double epsilonPrime, double l, size_t k,
                         size_t num_nodes, mpi_omp_parallel_tag &) {
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  return (ThetaPrime(x, epsilonPrime, l, k, num_nodes, omp_parallel_tag{}) /
          world_size) + 1;
}


template <typename GraphTy, typename PRNGeneratorTy, typename diff_model_tag>
auto Sampling(const GraphTy &G, std::size_t k, double epsilon, double l,
              PRNGeneratorTy &generator, IMMExecutionRecord &record,
              diff_model_tag &&model_tag, mpi_omp_parallel_tag &ex_tag) {
  using vertex_type = typename GraphTy::vertex_type;

  // sqrt(2) * epsilon
  double epsilonPrime = 1.4142135623730951 * epsilon;

  double LB = 0;
  std::vector<RRRset<GraphTy>> RR;

  auto start = std::chrono::high_resolution_clock::now();
  size_t thetaPrime = 0;
  for (ssize_t x = 1; x < std::log2(G.num_nodes()); ++x) {
    // Equation 9
    ssize_t thetaPrime = ThetaPrime(x, epsilonPrime, l, k, G.num_nodes(),
                                    ex_tag);

    auto deltaRR = GenerateRRRSets(G, thetaPrime - RR.size(), generator,
                                   std::forward<diff_model_tag>(model_tag),
                                   omp_parallel_tag{});

    RR.insert(RR.end(), std::make_move_iterator(deltaRR.begin()),
              std::make_move_iterator(deltaRR.end()));

    const auto &S = FindMostInfluentialSet(G, k, RR, ex_tag);
    double f = S.first;

    if (f >= std::pow(2, -x)) {
      LB = (G.num_nodes() * f) / (1 + epsilonPrime);
      break;
    }
  }

  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  size_t theta = Theta(epsilon, l, k, LB, G.num_nodes());
  size_t thetaLocal = (theta / world_size) + 1;
  auto end = std::chrono::high_resolution_clock::now();

  record.ThetaEstimation = end - start;

  record.Theta = theta;

  start = std::chrono::high_resolution_clock::now();
  if (thetaLocal > RR.size()) {
    auto deltaRR = GenerateRRRSets(G, thetaLocal - RR.size(), generator,
                                   std::forward<diff_model_tag>(model_tag),
                                   omp_parallel_tag{});

    RR.insert(RR.end(), std::make_move_iterator(deltaRR.begin()),
              std::make_move_iterator(deltaRR.end()));
  }
  end = std::chrono::high_resolution_clock::now();

  record.GenerateRRRSets = end - start;

  return RR;
}


template <typename GraphTy, typename diff_model_tag, typename PRNG>
auto IMM(const GraphTy &G, std::size_t k, double epsilon, double l, PRNG &gen,
         diff_model_tag &&model_tag, im::mpi_omp_parallel_tag &&ex_tag) {
  using vertex_type = typename GraphTy::vertex_type;
  IMMExecutionRecord record;

  size_t max_num_threads(1);

#pragma omp single
  max_num_threads = omp_get_max_threads();

  // Find out rank, size
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  gen.split(world_size, world_rank);

  std::vector<trng::lcg64> generator(max_num_threads, gen);

#pragma omp parallel
  {
    generator[omp_get_thread_num()].split(omp_get_num_threads(),
                                          omp_get_thread_num());
  }

  l = l * (1 + 1 / std::log2(G.num_nodes()));

  const auto &R = Sampling(G, k, epsilon, l, generator, record,
                           std::forward<diff_model_tag>(model_tag),
                           ex_tag);

  auto start = std::chrono::high_resolution_clock::now();
  const auto &S = FindMostInfluentialSet(G, k, R, ex_tag);
  auto end = std::chrono::high_resolution_clock::now();

  record.FindMostInfluentialSet = end - start;

  return std::make_pair(S.second, record);
}

}  // namespace im

#endif  // IM_MPI_IMM_H