//===------------------------------------------------------------*- C++ -*-===//
//
//             Ripples: A C++ Library for Influence Maximization
//                  Marco Minutoli <marco.minutoli@pnnl.gov>
//                   Pacific Northwest National Laboratory
//
//===----------------------------------------------------------------------===//
//
// Copyright (c) 2019, Battelle Memorial Institute
//
// Battelle Memorial Institute (hereinafter Battelle) hereby grants permission
// to any person or entity lawfully obtaining a copy of this software and
// associated documentation files (hereinafter “the Software”) to redistribute
// and use the Software in source and binary forms, with or without
// modification.  Such person or entity may use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and may permit
// others to do so, subject to the following conditions:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimers.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Other than as used herein, neither the name Battelle Memorial Institute or
//    Battelle may be used in any form whatsoever without the express written
//    consent of Battelle.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL BATTELLE OR CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//===----------------------------------------------------------------------===//

#ifndef RIPPLES_BATCHED_ADD_RRRSET_H
#define RIPPLES_BATCHED_ADD_RRRSET_H

#include <cassert>
#include <algorithm>
#include <numeric>
#include <string>
#include <ostream>
#include <map>
#include <unordered_map>

#include "trng/lcg64.hpp"
#include "trng/uniform_int_dist.hpp"
#include "trng/uniform01_dist.hpp"

#define NEIGHBOR_COLOR

namespace ripples {

template <typename GraphTy, typename SItrTy, typename OItrTy,
          typename PRNGeneratorTy, typename diff_model_tag>
void BatchedBFS(const GraphTy &G, SItrTy B, SItrTy E, OItrTy O,
                PRNGeneratorTy& generator,
                diff_model_tag &&tag) {
  assert(std::distance(B, E) <= 64 && "Only up to 64 BFS are supported");
  using vertex_type = typename GraphTy::vertex_type;
  std::vector<std::vector<bool>> visited_matrix(std::distance(B, E), std::vector<bool>(G.num_nodes(), false));

  // using frontier_element = std::pair<vertex_type, uint64_t>;
  using frontier_element = vertex_type;
  std::unordered_map<vertex_type, uint64_t> color_map, new_color_map;

  trng::uniform01_dist<float> value;

  uint64_t color = 1ul << 63;
  std::vector<frontier_element> frontier, new_frontier;
  for (auto itr = B; itr < E; ++itr, color /= 2) {
    auto pos = color_map.find(*itr);
    if (pos == color_map.end()) {
      frontier.push_back(*itr);
      color_map[*itr] = color;
    } else {
      color_map[*itr] |= color;
    }
    visited_matrix[__builtin_clzl(color)][*itr] = true;
    (O + __builtin_clzl(color))->push_back(*itr);
  }

  assert(frontier.size() != 0);

  while (frontier.size() != 0) {
    new_frontier.resize(0);
    new_color_map.clear();
    // The compacted frontier is now in `frontier`
    std::for_each(frontier.begin(), frontier.end(), [&](const auto & v) {
      // auto vertex = v.first;
      // auto colors = v.second;
      auto vertex = v;
      auto colors = color_map[vertex];

      if (std::is_same<diff_model_tag, ripples::independent_cascade_tag>::value) {
        while (colors != 0) {
          uint64_t color = __builtin_clzl(colors);

          for (auto u : G.neighbors(vertex)) {
            #ifdef NEIGHBOR_COLOR
            if (!visited_matrix[color][u.vertex] && value(generator[0]) <= u.weight) {
            #else
            if (!visited_matrix[color][u.vertex] && value(generator) <= u.weight) {
            #endif
              visited_matrix[color][u.vertex] = true;
              (O + color)->push_back(u.vertex);
              auto pos = new_color_map.find(u.vertex);
              if (pos == new_color_map.end()) {
                new_frontier.push_back(u.vertex);
                new_color_map[u.vertex] = (1ul << ((sizeof(colors) * 8 - 1) - color));
              } else {
                new_color_map[u.vertex] |= (1ul << ((sizeof(colors) * 8 - 1) - color));
              }
            }
          }

          colors -= (1ul << ((sizeof(colors) * 8 - 1) - color));
        }
      } else if (std::is_same<diff_model_tag, ripples::linear_threshold_tag>::value) {
        while (colors != 0) {
          uint64_t color = __builtin_clzl(colors);

          #ifdef NEIGHBOR_COLOR
          float threshold = value(generator[0]);
          #else
          float threshold = value(generator);
          #endif
          for (auto u : G.neighbors(vertex)) {
            threshold -= u.weight;
            if (threshold > 0) continue;

            if (!visited_matrix[color][u.vertex]) {
              visited_matrix[color][u.vertex] = true;
              (O + color)->push_back(u.vertex);
              auto pos = new_color_map.find(u.vertex);
              if (pos == new_color_map.end()) {
                new_frontier.push_back(u.vertex);
                new_color_map[u.vertex] = (1ul << ((sizeof(colors) * 8 - 1) - color));
              } else {
                new_color_map[u.vertex] |= (1ul << ((sizeof(colors) * 8 - 1) - color));
              }
            }
            break;
          }

          colors -= (1ul << ((sizeof(colors) * 8 - 1) - color));
        }
      } else {
        throw;
      }
    });

    std::swap(color_map, new_color_map);
    std::swap(frontier, new_frontier);
  }

  for (int i = 0; i < std::distance(B, E); ++i, ++O) {
    std::sort(O->begin(), O->end());
  }
}

template <typename GraphTy, typename SItrTy, typename OItrTy,
          typename PRNGeneratorTy, typename diff_model_tag>
void BatchedBFSNeighborColor(const GraphTy &G, SItrTy B, SItrTy E, OItrTy O,
                PRNGeneratorTy& generator,
                diff_model_tag &&tag) {
  assert(std::distance(B, E) <= 64 && "Only up to 64 BFS are supported");
  using vertex_type = typename GraphTy::vertex_type;
  // std::cout << "Vector init" << std::endl;
  std::vector<uint64_t> visited_matrix(G.num_nodes(), 0);

  // using frontier_element = std::pair<vertex_type, uint64_t>;
  using frontier_element = vertex_type;
  // std::cout << "Unordered init" << std::endl;
  std::unordered_map<vertex_type, uint64_t> color_map, new_color_map;

  trng::uniform01_dist<float> value;

  // std::cout << "BFS: " << std::distance(B, E) << std::endl;

  uint64_t color = 1ul << 63;
  // std::vector<frontier_element> frontier, new_frontier;
  // frontier.reserve(G.num_nodes());
  // new_frontier.reserve(G.num_nodes());
  for (auto itr = B; itr < E; ++itr, color >>= 1) {
    auto pos = color_map.find(*itr);
    if (pos == color_map.end()) {
      // frontier.push_back(*itr);
      color_map[*itr] = color;
    } else {
      color_map[*itr] |= color;
    }
    visited_matrix[*itr] |= color;
  }

  assert(!frontier.empty())

  while (!color_map.empty()) {
    new_color_map.clear();
    // Iterate over color_map
    for (auto& [vertex, colors] : color_map) {

      // Convert the colors to an array of color masks
      uint64_t color_masks[64];
      uint64_t num_colors = 0;
      while (colors != 0) {
        uint64_t color = __builtin_clzl(colors);
        color_masks[num_colors++] = (1ul << ((sizeof(colors) * 8 - 1) - color));
        colors -= (1ul << ((sizeof(colors) * 8 - 1) - color));
      }

      for (auto u : G.neighbors(vertex)) {
        const uint64_t visited_old = visited_matrix[u.vertex];
        uint64_t visited_new = 0;
        #pragma omp simd reduction(|:visited_new)
        for(size_t i = 0; i < num_colors; ++i) {
          const uint64_t color_mask = color_masks[i];
          if(!(visited_old & color_mask) && value(generator[i]) <= u.weight) {
            visited_new |= color_mask;
          }
        }
        if(visited_new != 0){
          visited_matrix[u.vertex] |= visited_new;
          auto pos = new_color_map.find(u.vertex);
          if (pos == new_color_map.end()) {
            new_color_map[u.vertex] = visited_new;
          } else {
            pos->second |= visited_new;
          }
        }
      }
    }

    std::swap(color_map, new_color_map);
  }

  // Traverse visited_matrix and push the vertices into the output
  for (int i = 0; i < G.num_nodes(); ++i) {
    uint64_t colors = visited_matrix[i];
    while (colors != 0) {
      uint64_t color = __builtin_clzl(colors);
      (O + color)->push_back(i);
      colors -= (1ul << ((sizeof(colors) * 8 - 1) - color));
    }
  }
}

}

#endif
