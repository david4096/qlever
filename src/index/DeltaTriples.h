// Copyright 2023, University of Freiburg
// Chair of Algorithms and Data Structures
// Authors: Hannah Bast <bast@cs.uni-freiburg.de>

#pragma once

#include "engine/LocalVocab.h"
#include "global/IdTriple.h"
#include "index/Index.h"
#include "index/IndexBuilderTypes.h"
#include "index/LocatedTriples.h"
#include "index/Permutations.h"
#include "parser/TurtleParser.h"
#include "util/HashSet.h"

// A class for maintaining triples that are inserted or deleted after index
// building, we call these delta triples. How it works in principle:
//
// 1. For each delta triple, find the location in each permutation (block index
// and index within that block, see end of the file for an exact definition).
//
// 2. For each permutation and each block, store a sorted list of the positions
// of the delta triples within that block.
//
// 3. In the call of `PermutationImpl::scan`, use the respective lists to merge
// the relevant delta tripless into the index scan result.
//
class DeltaTriples {
 private:
  // The index to which these triples are added.
  const Index& index_;

  // The local vocabulary of the delta triples (they may have components,
  // which are not contained in the vocabulary of the original index).
  LocalVocab localVocab_;

  // The positions of the delta triples in each of the six permutations.
  LocatedTriplesPerBlock locatedTriplesPerBlockInPSO_;
  LocatedTriplesPerBlock locatedTriplesPerBlockInPOS_;
  LocatedTriplesPerBlock locatedTriplesPerBlockInSPO_;
  LocatedTriplesPerBlock locatedTriplesPerBlockInSOP_;
  LocatedTriplesPerBlock locatedTriplesPerBlockInOSP_;
  LocatedTriplesPerBlock locatedTriplesPerBlockInOPS_;

  // Each delta triple needs to know where it is stored in each of the six
  // `LocatedTriplesPerBlock` above.
  struct LocatedTripleHandles {
    LocatedTriples::iterator forPSO;
    LocatedTriples::iterator forPOS;
    LocatedTriples::iterator forSPO;
    LocatedTriples::iterator forSOP;
    LocatedTriples::iterator forOPS;
    LocatedTriples::iterator forOSP;
  };

  // The sets of triples added to and subtracted from the original index
  //
  // NOTE: The methods `insertTriple` and `deleteTriple` make sure that only
  // triples are added that are not already contained in the original index and
  // that only triples are subtracted that are contained in the original index.
  // In particular, no triple can be in both of these sets.
  ad_utility::HashMap<IdTriple, LocatedTripleHandles> triplesInserted_;
  ad_utility::HashMap<IdTriple, LocatedTripleHandles> triplesDeleted_;

 public:
  // Construct for given index.
  DeltaTriples(const Index& index) : index_(index) {}

  // Get the `Index` to which these delta triples refer.
  const Index& getIndex() const { return index_; }

  // Get the common `LocalVocab` of the delta triples.
  LocalVocab& localVocab() { return localVocab_; }
  const LocalVocab& localVocab() const { return localVocab_; }

  // Clear `_triplesAdded` and `_triplesSubtracted` and all associated data
  // structures.
  void clear();

  // The number of delta triples added and subtracted.
  size_t numInserted() const { return triplesInserted_.size(); }
  size_t numDeleted() const { return triplesDeleted_.size(); }

  // Insert triple.
  void insertTriple(TurtleTriple turtleTriple);

  // Delete triple.
  void deleteTriple(TurtleTriple turtleTriple);

  // Get `TripleWithPosition` objects for given permutation.
  const LocatedTriplesPerBlock& getTriplesWithPositionsPerBlock(
      Permutation::Enum permutation) const;

  // TODO: made public as long as we are trying to figure out how this works.
 private:
 public:
  // Get triples of `Id`s from `TurtleTriple` (which is the kind of triple we
  // get from `TurtleParser`, see the code currently handling insertions and
  // deletions in `Server.cpp`).
  //
  // NOTE: This is not `const` because translating to IDs may augment the local
  // vocabulary.
  IdTriple getIdTriple(const TurtleTriple& turtleTriple);

  // Find the position of the given triple in the given permutation and add it
  // to each of the six `LocatedTriplesPerBlock` maps (one per permutation).
  // Return the iterators of where it was added (so that we can easily delete it
  // again from these maps later).
  //
  // TODO: The function is name is misleading, since this method does not only
  // locate, but also add to the mentioned data structures.
  LocatedTripleHandles locateTripleInAllPermutations(const IdTriple& idTriple);

  // Erase `LocatedTriple` object from each `LocatedTriplesPerBlock` list. The
  // argument are iterators for each list, as returned by the method
  // `locateTripleInAllPermutations` above.
  //
  // NOTE: The iterators are invalid afterwards. That is OK, as long as we also
  // delete the respective entry in `triplesInserted_` or `triplesDeleted_`,
  // which stores these iterators.
  void eraseTripleInAllPermutations(LocatedTripleHandles& handles);
};

// More detailed discussion and information about the `DeltaTriples` class.
//
// A. DELTA TRIPLES AND THE CACHE
//
// For now, our approach only works when the results of index scans are not
// cached (unless there are no relevant delta triples for a particular scan).
// There are two ways how this can play out in the future:
//
// Either we generally do not cache the results of index scans anymore. This
// would have various advantages, in particular, joining with something like
// `rdf:type` would then be possible without storing the whole relation in
// RAM. However, we need a faster decompression then and maybe a smaller block
// size (currently 8 MB).
//
// Or we add the delta triples when iterating over the cached (uncompressed)
// result from the index scan. In that case, we would need to (in Step 1 above)
// store and maintain the positions in those uncompressed index scans. However,
// this would only work for the results of index scans. For the results of more
// complex subqueries, it's hard to figure out which delta triples are relevant.
//
// B. DEFINITION OF THE POSITION OF A DELTA TRIPLE IN A PERMUTATION
//
// 1. The position is defined by the index of a block in the permutation and the
// index of a row within that block.
//
// 2. If the triple in contained in the permutation, it is contained exactly
// once and so there is a well defined block and position in that block.
//
// 2. If there is a block, where the first triple is smaller and the last triple
// is larger, then that is the block and the position in that block is that of
// the first triple that is (not smaller and hence) larger.
//
// 3. If the triple falls "between two blocks" (the last triple of the previous
// block is smaller and the first triple of the next block is larger), then the
// position is the first position in that next block.
//
// 4. As a special case of 3., if the triple is smaller than all triples in the
// permutation, the position is the first position of the first block.
//
// 5. If the triple is larger than all triples in the permutation, the block
// index is one after the largest block index and the position within that
// non-existing block is arbitrary.