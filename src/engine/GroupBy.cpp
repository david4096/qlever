// Copyright 2018, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Florian Kramer (florian.kramer@mail.uni-freiburg.de)

#include "GroupBy.h"

#include "../util/HashSet.h"
#include "../util/Conversions.h"
#include "../index/Index.h"

GroupBy::GroupBy(QueryExecutionContext *qec,
                 std::shared_ptr<QueryExecutionTree> subtree,
                 const vector<string>& groupByVariables,
                 const std::vector<ParsedQuery::Alias>& aliases) :
  Operation(qec),
  _subtree(subtree),
  _groupByVariables(groupByVariables) {

  _aliases.reserve(aliases.size());
  for (const ParsedQuery::Alias &a: aliases) {
    if (a._isAggregate) {
      _aliases.push_back(a);
    }
  }
  std::sort(_aliases.begin(), _aliases.end(),
            [](const ParsedQuery::Alias& a1, const ParsedQuery::Alias& a2) {
    return a1._outVarName < a2._outVarName;
  });

  // sort the groupByVariables to ensure the cache key is order invariant
  std::sort(_groupByVariables.begin(), _groupByVariables.end());

  // The returned columns are all groupByVariables followed by aggregrates
  size_t colIndex = 0;
  for (std::string var : _groupByVariables) {
    _varColMap[var] = colIndex;
    colIndex++;
  }
  for (const ParsedQuery::Alias& a : _aliases) {
    _varColMap[a._outVarName] = colIndex;
    colIndex++;
  }
}

string GroupBy::asString(size_t indent) const {
  std::ostringstream os;
  for (size_t i = 0; i < indent; ++i) { os << " "; }
  os << "GROUP_BY" << std::endl;
  for (const std::string var : _groupByVariables) {
    os << var << ", ";
  }
  for (auto p : _aliases) {
    os << p._function << ", ";
  }
  os << std::endl;
  os << _subtree->asString(indent);
  return os.str();
}

size_t GroupBy::getResultWidth() const {
  //TODO(Florian): stub
  return _varColMap.size();
}

size_t GroupBy::resultSortedOn() const {
  //TODO(Florian): stub
  return 0;
}

vector<pair<size_t, bool>> GroupBy::computeSortColumns(
    std::shared_ptr<QueryExecutionTree> subtree,
    const vector<string>& groupByVariables,
    const std::vector<ParsedQuery::Alias>& aliases) {
  // Create sorted lists of the aliases and the group by variables to determine
  // the output column order, on which the sorting depends. Then populate
  // the vector of columns which should be sorted by using the subtrees
  // variable column map.
  vector<pair<size_t, bool>> cols;
  if (groupByVariables.empty()) {
    // the entire input is a single group, no sorting needs to be done
    return cols;
  }

  std::vector<ParsedQuery::Alias> sortedAliases;
  sortedAliases.reserve(aliases.size());
  for (const ParsedQuery::Alias &a : aliases) {
    if (a._isAggregate) {
      sortedAliases.push_back(a);
    }
  }
  std::sort(sortedAliases.begin(), sortedAliases.end(),
            [](const ParsedQuery::Alias& a1, const ParsedQuery::Alias& a2) {
    return a1._outVarName < a2._outVarName;
  });

  std::vector<std::string> sortedGroupByVars;
  sortedGroupByVars.insert(sortedGroupByVars.end(), groupByVariables.begin(),
                           groupByVariables.end());

  // sort the groupByVariables to ensure the cache key is order invariant
  std::sort(sortedGroupByVars.begin(), sortedGroupByVars.end());


  std::unordered_map<string, size_t> inVarColMap = subtree->getVariableColumnMap();


  // The returned columns are all groupByVariables followed by aggregrates
  for (std::string var : sortedGroupByVars) {
    cols.push_back({inVarColMap[var], false});
  }
  for (const ParsedQuery::Alias& a : sortedAliases) {
    cols.push_back({inVarColMap[a._outVarName], false});
  }
  return cols;
}


std::unordered_map<string, size_t> GroupBy::getVariableColumns() const {
  return _varColMap;
}

float GroupBy::getMultiplicity(size_t col) {
  //TODO(Florian): stub
  (void)col;
  return 0;
}

size_t GroupBy::getSizeEstimate() {
  //TODO(Florian): stub
  return 0;
}

size_t GroupBy::getCostEstimate() {
  //TODO(Florian): stub
  return 0;
}

template<typename T, typename C>
struct resizeIfVec {
  static void resize(T& t, int size) { (void) t; (void) size; }
};

template<typename C>
struct resizeIfVec<vector<C>, C> {
  static void resize(vector<C>& t, int size) {
    t.resize(size);
  }
};

template<typename A, typename R>
void processGroup(const GroupBy::Aggregate& a, size_t blockStart,
                  size_t blockEnd, const vector<A>* input,
                  const vector<ResultTable::ResultType>& inputTypes,
                  R& resultRow,
                  const ResultTable* inTable,
                  ResultTable* outTable,
                  const Index& index,
                  ad_utility::HashSet<size_t>& distinctHashSet) {
  if (a._distinct && distinctHashSet.size() < blockEnd - blockStart) {
    // TODO(florian): This may not be very efficient
    distinctHashSet.resize(blockEnd - blockStart);
  }
  switch (a._type) {
  case GroupBy::AggregateType::AVG: {
    float res = 0;
    if (inputTypes[a._inCol] == ResultTable::ResultType::VERBATIM) {
      if (a._distinct) {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            res += (*input)[i][a._inCol];
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          res += (*input)[i][a._inCol];
        }
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::FLOAT) {
      if (a._distinct) {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            res += *reinterpret_cast<const float*>(&(*input)[i][a._inCol]);
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          res += *reinterpret_cast<const float*>(&(*input)[i][a._inCol]);
        }
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::TEXT
               || inputTypes[a._inCol] == ResultTable::ResultType::STRING) {
      res = std::numeric_limits<float>::quiet_NaN();
    } else {
      if (a._distinct) {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            // load the string, parse it as an xsd::int or float
            std::string entity = index.idToString((*input)[i][a._inCol]);
            if (!ad_utility::startsWith(entity, VALUE_FLOAT_PREFIX)) {
              res = std::numeric_limits<float>::quiet_NaN();
              break;
            } else {
              res += ad_utility::convertIndexWordToFloatValue(entity.substr(0, entity.size() - 1));
            }
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          // load the string, parse it as an xsd::int or float
          std::string entity = index.idToString((*input)[i][a._inCol]);
          if (!ad_utility::startsWith(entity, VALUE_FLOAT_PREFIX)) {
            res = std::numeric_limits<float>::quiet_NaN();
            break;
          } else {
            res += ad_utility::convertIndexWordToFloatValue(entity.substr(0, entity.size() - 1));
          }
        }
      }
    }
    res /= (blockEnd - blockStart + 1);

    resultRow[a._outCol] = 0;
    *reinterpret_cast<float*>(&resultRow[a._outCol]) = res;
    break;
  }
  case GroupBy::AggregateType::COUNT:
    if (a._distinct) {
      size_t count = 0;
      for (size_t i = blockStart; i <= blockEnd; i++) {
        const auto it = distinctHashSet.find((*input)[i][a._inCol]);
        if (it == distinctHashSet.end()) {
          count++;
          distinctHashSet.insert((*input)[i][a._inCol]);
        }
      }
      resultRow[a._outCol] = count;
      distinctHashSet.clear();
    } else {
      resultRow[a._outCol] = blockEnd - blockStart + 1;
    }
    break;
  case GroupBy::AggregateType::GROUP_CONCAT: {
    std::ostringstream out;
    std::string *delim = reinterpret_cast<string*>(a._userData);
    if (inputTypes[a._inCol] == ResultTable::ResultType::VERBATIM) {
      if (a._distinct) {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            out << (*input)[i][a._inCol] << *delim;
          }
        }
        const auto it = distinctHashSet.find((*input)[blockEnd][a._inCol]);
        if (it == distinctHashSet.end()) {
          out << (*input)[blockEnd][a._inCol] << *delim;
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          out << (*input)[i][a._inCol] << *delim;
        }
        out << (*input)[blockEnd][a._inCol];
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::FLOAT) {
      float f;
      if (a._distinct) {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            std::memcpy(&f, &(*input)[i][a._inCol], sizeof(float));
            out << f << *delim;
          }
        }
        const auto it = distinctHashSet.find((*input)[blockEnd][a._inCol]);
        if (it == distinctHashSet.end()) {
          std::memcpy(&f, &(*input)[blockEnd][a._inCol], sizeof(float));
          out << f;
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          std::memcpy(&f, &(*input)[i][a._inCol], sizeof(float));
          out << f << *delim;
        }
        std::memcpy(&f, &(*input)[blockEnd][a._inCol], sizeof(float));
        out << f;
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::TEXT) {
      if (a._distinct) {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            out << index.getTextExcerpt((*input)[i][a._inCol]) << *delim;
          }
        }
        const auto it = distinctHashSet.find((*input)[blockEnd][a._inCol]);
        if (it == distinctHashSet.end()) {
          out << index.getTextExcerpt((*input)[blockEnd][a._inCol]);
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          out << index.getTextExcerpt((*input)[i][a._inCol]) << *delim;
        }
        out << index.getTextExcerpt((*input)[blockEnd][a._inCol]);
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::STRING){
      if (a._distinct) {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            out << inTable->idToString((*input)[i][a._inCol]) << *delim;
          }
        }
        const auto it = distinctHashSet.find((*input)[blockEnd][a._inCol]);
        if (it == distinctHashSet.end()) {
          out << inTable->idToString((*input)[blockEnd][a._inCol]);
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          out << inTable->idToString((*input)[i][a._inCol]) << *delim;
        }
        out << inTable->idToString((*input)[blockEnd][a._inCol]);
      }
    } else {
      if (a._distinct) {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            std::string entity = index.idToString((*input)[i][a._inCol]);
            if (ad_utility::startsWith(entity, VALUE_PREFIX)) {
              out << ad_utility::convertIndexWordToValueLiteral(entity) << *delim;
            } else {
              out << entity << *delim;
            }
          }
        }
        const auto it = distinctHashSet.find((*input)[blockEnd][a._inCol]);
        if (it == distinctHashSet.end()) {
          std::string entity = index.idToString((*input)[blockEnd][a._inCol]);
          if (ad_utility::startsWith(entity, VALUE_PREFIX)) {
            out << ad_utility::convertIndexWordToValueLiteral(entity);
          } else {
            out << entity;
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i + 1 <= blockEnd; i++) {
          std::string entity = index.idToString((*input)[i][a._inCol]);
          if (ad_utility::startsWith(entity, VALUE_PREFIX)) {
            out << ad_utility::convertIndexWordToValueLiteral(entity) << *delim;
          } else {
            out << entity << *delim;
          }
        }
        std::string entity = index.idToString((*input)[blockEnd][a._inCol]);
        if (ad_utility::startsWith(entity, VALUE_PREFIX)) {
          out << ad_utility::convertIndexWordToValueLiteral(entity);
        } else {
          out << entity;
        }
      }
    }
    resultRow[a._outCol] = outTable->_localVocab.size();
    outTable->_localVocab.push_back(out.str());
    break;
  }
  case GroupBy::AggregateType::MAX: {
    if (inputTypes[a._inCol] == ResultTable::ResultType::VERBATIM) {
      Id res = std::numeric_limits<Id>::lowest();
      for (size_t i = blockStart; i <= blockEnd; i++) {
        res = std::max(res, (*input)[i][a._inCol]);
      }
      resultRow[a._outCol] = res;
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::FLOAT) {
      float res = std::numeric_limits<float>::lowest();
      for (size_t i = blockStart; i <= blockEnd; i++) {
        // interpret the first sizeof(float) bytes of the entry as a float.
        res = std::max(res, *reinterpret_cast<const float*>(&(*input)[i][a._inCol]));
      }
      resultRow[a._outCol] = 0;
      std::memcpy(&resultRow[a._outCol], &res, sizeof(float));
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::TEXT
               || inputTypes[a._inCol] == ResultTable::ResultType::STRING) {
      resultRow[a._outCol] = ID_NO_VALUE;
    } else {
      Id res = std::numeric_limits<Id>::lowest();
      for (size_t i = blockStart; i <= blockEnd; i++) {
        res = std::max(res, (*input)[i][a._inCol]);
      }
      resultRow[a._outCol] = res;
    }
    break;
  }
  case GroupBy::AggregateType::MIN: {
    if (inputTypes[a._inCol] == ResultTable::ResultType::VERBATIM) {
      Id res = std::numeric_limits<Id>::max();
      for (size_t i = blockStart; i <= blockEnd; i++) {
        res = std::min(res, (*input)[i][a._inCol]);
      }
      resultRow[a._outCol] = res;
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::FLOAT) {
      float res = std::numeric_limits<float>::max();
      for (size_t i = blockStart; i <= blockEnd; i++) {
        // interpret the first sizeof(float) bytes of the entry as a float.
        res = std::min(res, *reinterpret_cast<const float*>(&(*input)[i][a._inCol]));
      }
      resultRow[a._outCol] = 0;
      std::memcpy(&resultRow[a._outCol], &res, sizeof(float));
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::TEXT
               || inputTypes[a._inCol] == ResultTable::ResultType::STRING) {
      resultRow[a._outCol] = ID_NO_VALUE;
    } else {
      Id res = std::numeric_limits<Id>::max();
      for (size_t i = blockStart; i <= blockEnd; i++) {
        res = std::min(res, (*input)[i][a._inCol]);
      }
      resultRow[a._outCol] = res;
    }
    break;
  }
  case GroupBy::AggregateType::SAMPLE:
    resultRow[a._outCol] = (*input)[blockEnd][a._inCol];
    break;
  case GroupBy::AggregateType::SUM: {
    float res = 0;
    if (inputTypes[a._inCol] == ResultTable::ResultType::VERBATIM) {
      if (a._distinct) {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            res += (*input)[i][a._inCol];
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          res += (*input)[i][a._inCol];
        }
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::FLOAT) {
      if (a._distinct) {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
             res += *reinterpret_cast<const float*>(&(*input)[i][a._inCol]);
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i <= blockEnd; i++) {
           res += *reinterpret_cast<const float*>(&(*input)[i][a._inCol]);
        }
      }
    } else if (inputTypes[a._inCol] == ResultTable::ResultType::TEXT
               || inputTypes[a._inCol] == ResultTable::ResultType::STRING) {
      res = std::numeric_limits<float>::quiet_NaN();
    } else {
      if (a._distinct) {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          const auto it = distinctHashSet.find((*input)[i][a._inCol]);
          if (it == distinctHashSet.end()) {
            distinctHashSet.insert((*input)[i][a._inCol]);
            // load the string, parse it as an xsd::int or float
            std::string entity = index.idToString((*input)[i][a._inCol]);
            if (!ad_utility::startsWith(entity, VALUE_FLOAT_PREFIX)) {
              res = std::numeric_limits<float>::quiet_NaN();
              break;
            } else {
              res += ad_utility::convertIndexWordToFloatValue(entity.substr(0, entity.size() - 1));
            }
          }
        }
        distinctHashSet.clear();
      } else {
        for (size_t i = blockStart; i <= blockEnd; i++) {
          // load the string, parse it as an xsd::int or float
          std::string entity = index.idToString((*input)[i][a._inCol]);
          if (!ad_utility::startsWith(entity, VALUE_FLOAT_PREFIX)) {
            res = std::numeric_limits<float>::quiet_NaN();
            break;
          } else {
            res += ad_utility::convertIndexWordToFloatValue(entity.substr(0, entity.size() - 1));
          }
        }
      }
    }
    resultRow[a._outCol] = 0;
    *reinterpret_cast<float*>(&resultRow[a._outCol]) = res;
    break;
  }
  case GroupBy::AggregateType::FIRST:
    // This does the same as sample, as the non grouping rows have no
    // inherent order.
    resultRow[a._outCol] = (*input)[blockStart][a._inCol];
    break;
  case GroupBy::AggregateType::LAST:
    // This does the same as sample, as the non grouping rows have no
    // inherent order.
    resultRow[a._outCol] = (*input)[blockEnd][a._inCol];
    break;
  }
}

template<typename A, typename R>
void doGroupBy(const vector<A>* input,
               const vector<ResultTable::ResultType>& inputTypes,
               const vector<size_t>& groupByCols,
               const vector<GroupBy::Aggregate>& aggregates,
               vector<R>* result,
               const ResultTable* inTable,
               ResultTable* outTable,
               const Index& index) {
  if (input->size() == 0) {
    return;
  }
  ad_utility::HashSet<size_t> distinctHashSet;

  if (groupByCols.empty()) {
    // The entire input is a single group
    size_t blockStart = 0;
    size_t blockEnd = input->size();
    result->emplace_back();
    R& resultRow = result->back();
    // does nothing for arrays, resizes vectors
    resizeIfVec<R, typename R::value_type>::resize(resultRow,
                                                   aggregates.size());
    for (const GroupBy::Aggregate &a : aggregates) {
      processGroup<A, R>(a, blockStart, blockEnd, input, inputTypes, resultRow,
                        inTable, outTable, index, distinctHashSet);
    }
    return;
  }

  std::vector<std::pair<size_t, Id>> currentGroupBlock;
  // TODO(florian): optimize this for empty groupByCols
  for (size_t col : groupByCols) {
    currentGroupBlock.push_back(std::pair<size_t, Id>(col, (*input)[0][col]));
  }
  size_t blockStart = 0;
  size_t blockEnd = 0;
  for (size_t pos = 1; pos < input->size(); pos++) {
    bool rowMatchesCurrentBlock = true;
    for (size_t i = 0; i < currentGroupBlock.size(); i++) {
      if ((*input)[pos][currentGroupBlock[i].first] != currentGroupBlock[i].second) {
        rowMatchesCurrentBlock = false;
        break;
      }
    }
    if (!rowMatchesCurrentBlock) {
      result->emplace_back();
      R& resultRow = result->back();
      // does nothing for arrays, resizes vectors
      resizeIfVec<R, typename R::value_type>::resize(resultRow,
                                                     groupByCols.size()
                                                     +  aggregates.size());
      blockEnd = pos - 1;
      for (const GroupBy::Aggregate &a : aggregates) {
        processGroup<A, R>(a, blockStart, blockEnd, input, inputTypes, resultRow,
                          inTable, outTable, index, distinctHashSet);
      }
      // setup for processing the next block
      blockStart = pos;
      for (size_t i = 0; i < currentGroupBlock.size(); i++) {
        currentGroupBlock[i].second = (*input)[pos][currentGroupBlock[i].first];
      }
    }
  }
  blockEnd = input->size() - 1;
  {
    result->emplace_back();
    R& resultRow = result->back();
    // does nothing for arrays, resizes vectors
    resizeIfVec<R, typename R::value_type>::resize(resultRow,
                                                   groupByCols.size()
                                                   +  aggregates.size());
    for (const GroupBy::Aggregate &a : aggregates) {
      processGroup<A, R>(a, blockStart, blockEnd, input, inputTypes, resultRow,
                        inTable, outTable, index, distinctHashSet);
    }
  }
}


// This struct is used to call doGroupBy with the correct template parameters.
// Using it is equivalent to a structure of nested if clauses, checking if
// the inputColCount and resultColCount are of a certain value, and then
// calling doGroupBy.
template<int InputColCount, int ResultColCount>
struct callDoGroupBy {
  static void call(int inputColCount, int resultColCount,
                   std::shared_ptr<const ResultTable> subresult,
                   const vector<ResultTable::ResultType>& inputTypes,
                   const vector<size_t>& groupByCols,
                   const vector<GroupBy::Aggregate>& aggregates,
                   ResultTable* result,
                   ResultTable* outTable,
                   const Index& index) {
    if (InputColCount == inputColCount) {
      if (resultColCount == ResultColCount) {
        result->_fixedSizeData = new vector<array<Id, ResultColCount>>();
        doGroupBy<array<Id, InputColCount>, array<Id, ResultColCount>>(
              static_cast<vector<array<Id, InputColCount>>*>(subresult->_fixedSizeData),
              inputTypes,
              groupByCols,
              aggregates,
              static_cast<vector<array<Id, ResultColCount>>*>(result->_fixedSizeData),
              subresult.get(),
              outTable,
              index);
      } else {
        callDoGroupBy<InputColCount, ResultColCount + 1>::call(inputColCount,
                                                               resultColCount,
                                                               subresult,
                                                               inputTypes,
                                                               groupByCols,
                                                               aggregates,
                                                               result,
                                                               outTable,
                                                               index);
      }
    } else {
      callDoGroupBy<InputColCount + 1, ResultColCount>::call(inputColCount,
                                                             resultColCount,
                                                             subresult,
                                                             inputTypes,
                                                             groupByCols,
                                                             aggregates,
                                                             result,
                                                             outTable,
                                                             index);
    }
  }
};

template<int ResultColCount>
struct callDoGroupBy<6, ResultColCount> {
  static void call(int inputColCount, int resultColCount,
                   std::shared_ptr<const ResultTable> subresult,
                   const vector<ResultTable::ResultType>& inputTypes,
                   const vector<size_t>& groupByCols,
                   const vector<GroupBy::Aggregate>& aggregates,
                   ResultTable* result,
                   ResultTable* outTable,
                   const Index& index) {
    if (resultColCount == ResultColCount) {
      result->_fixedSizeData = new vector<array<Id, ResultColCount>>();
      doGroupBy<vector<Id>, array<Id, ResultColCount>>(
            &subresult->_varSizeData,
            inputTypes,
            groupByCols,
            aggregates,
            static_cast<vector<array<Id, ResultColCount>>*>(result->_fixedSizeData),
            subresult.get(),
            outTable,
            index);
    } else {
      callDoGroupBy<6, ResultColCount + 1>::call(inputColCount,
                                                 resultColCount,
                                                 subresult,
                                                 inputTypes,
                                                 groupByCols,
                                                 aggregates,
                                                 result,
                                                 outTable,
                                                 index);
    }
  }
};

template<int InputColCount>
struct callDoGroupBy<InputColCount, 6> {
  static void call(int inputColCount, int resultColCount,
                   std::shared_ptr<const ResultTable> subresult,
                   const vector<ResultTable::ResultType>& inputTypes,
                   const vector<size_t>& groupByCols,
                   const vector<GroupBy::Aggregate>& aggregates,
                   ResultTable* result,
                   ResultTable* outTable,
                   const Index& index) {
    // avoid the quite numerous warnings about unused parameters
    (void) inputColCount;
    (void) resultColCount;
    doGroupBy<array<Id, InputColCount>, vector<Id>>(
          static_cast<vector<array<Id, InputColCount>>*>(subresult->_fixedSizeData),
          inputTypes,
          groupByCols,
          aggregates,
          &result->_varSizeData,
          subresult.get(),
          outTable,
          index);
  }
};

template<>
struct callDoGroupBy<6, 6> {
  static void call(int inputColCount, int resultColCount,
                   std::shared_ptr<const ResultTable> subresult,
                   const vector<ResultTable::ResultType>& inputTypes,
                   const vector<size_t>& groupByCols,
                   const vector<GroupBy::Aggregate>& aggregates,
                   ResultTable* result,
                   ResultTable* outTable,
                   const Index& index) {
    // avoid the quite numerous warnings about unused parameters
    (void) inputColCount;
    (void) resultColCount;
    doGroupBy<vector<Id>, vector<Id>>(
          &subresult->_varSizeData,
          inputTypes,
          groupByCols,
          aggregates,
          &result->_varSizeData,
          subresult.get(),
          outTable,
          index);
  }
};

void GroupBy::computeResult(ResultTable* result) const {
  std::vector<size_t> groupByColumns;

  result->_sortedBy = resultSortedOn();
  result->_nofColumns = getResultWidth();

  std::vector<Aggregate> aggregates;
  aggregates.reserve(_aliases.size() + _groupByVariables.size());

  // parse the group by columns
  std::unordered_map<string, Id> subtreeVarCols = _subtree->getVariableColumnMap();
  for (const string& var : _groupByVariables) {
    auto it = subtreeVarCols.find(var);
    if (it == subtreeVarCols.end()) {
      LOG(WARN) << "Group by variable " << var << " is not part of the query."
                << std::endl;
      if (result->_nofColumns == 1) {
        result->_fixedSizeData = new vector<array<Id, 1>>();
      } else if (result->_nofColumns == 2) {
        result->_fixedSizeData = new vector<array<Id, 2>>();
      } else if (result->_nofColumns == 3) {
        result->_fixedSizeData = new vector<array<Id, 3>>();
      } else if (result->_nofColumns == 4) {
        result->_fixedSizeData = new vector<array<Id, 4>>();
      } else if (result->_nofColumns == 5) {
        result->_fixedSizeData = new vector<array<Id, 5>>();
      }
      result->finish();
      return;
    }
    groupByColumns.push_back(it->second);
    // Add an "identity" aggregate in the form of a sample aggregate to
    // facilitate the passthrough of the groupBy columns into the result
    aggregates.emplace_back();
    aggregates.back()._type = AggregateType::SAMPLE;
    aggregates.back()._inCol = it->second;
    aggregates.back()._outCol = _varColMap.find(var)->second;
    aggregates.back()._userData = nullptr;
    aggregates.back()._distinct = false;
  }

  // parse the aggregate aliases
  for (const ParsedQuery::Alias& alias : _aliases) {
    if (alias._isAggregate) {
      aggregates.emplace_back();
      if (ad_utility::startsWith(alias._function, "COUNT")) {
        aggregates.back()._type = AggregateType::COUNT;
      } else if (ad_utility::startsWith(alias._function, "GROUP_CONCAT")) {
        aggregates.back()._type = AggregateType::GROUP_CONCAT;
      } else if (ad_utility::startsWith(alias._function, "SAMPLE")) {
        aggregates.back()._type = AggregateType::SAMPLE;
      } else if (ad_utility::startsWith(alias._function, "MIN")) {
        aggregates.back()._type = AggregateType::MIN;
      } else if (ad_utility::startsWith(alias._function, "MAX")) {
        aggregates.back()._type = AggregateType::MAX;
      } else if (ad_utility::startsWith(alias._function, "SUM")) {
        aggregates.back()._type = AggregateType::SUM;
      } else if (ad_utility::startsWith(alias._function, "AVG")) {
        aggregates.back()._type = AggregateType::AVG;
      } else {
        LOG(WARN) << "Unknown aggregate " << alias._function << std::endl;
        aggregates.pop_back();
        continue;
      }

      std::string inVarName;
      if (aggregates.back()._type == AggregateType::GROUP_CONCAT) {
        size_t varStart = alias._function.find('(');
        size_t varStop = alias._function.rfind(')');
        size_t delimitorPos = alias._function.find(';');
        if (varStop > varStart && varStop != std::string::npos
            && varStart != std::string::npos) {
          // found a matching pair of brackets
          // look for a distinct keyword

          if (alias._function.find("DISTINCT") != std::string::npos
              || alias._function.find("distinct") != std::string::npos) {
            aggregates.back()._distinct = true;
          } else {
            aggregates.back()._distinct = false;
          }

          if (delimitorPos != std::string::npos) {
            // found a delimiter, need to look for a separator assignment
            inVarName = alias._function.substr(varStart + 1,
                                               delimitorPos - varStart - 1);
            if (aggregates.back()._distinct) {
              inVarName = ad_utility::strip(inVarName, " \t").substr(8);
            }
            std::string concatString = alias._function.substr(delimitorPos + 1,
                                                              varStop - delimitorPos - 1);
            concatString = ad_utility::strip(concatString, " ");
            size_t startConcat = concatString.find('"');
            size_t stopConcat = concatString.rfind('"');
            if (stopConcat > startConcat && stopConcat != std::string::npos
                && startConcat != std::string::npos) {
              aggregates.back()._userData
                  = new std::string(concatString.substr(startConcat + 1,
                                                        stopConcat - startConcat - 1));
            } else {
              LOG(WARN) << "Unable to parse the delimiter in GROUP_CONCAT"
                           "aggregrate " << alias._function;
              aggregates.back()._userData = new std::string(" ");
            }
          } else {
            // found no delimiter, using the default separator ' '
            inVarName = alias._function.substr(varStart + 1,
                                               varStop - varStart - 1);
            if (aggregates.back()._distinct) {
              inVarName = ad_utility::strip(inVarName, " \t").substr(8);
            }
            aggregates.back()._userData = new std::string(" ");
          }

        }
      } else {
        size_t varStart = alias._function.find('(');
        size_t varStop = alias._function.rfind(')');
        if (varStop > varStart && varStop != std::string::npos
            && varStart != std::string::npos) {
          if (alias._function.find("DISTINCT") != std::string::npos
              || alias._function.find("distinct") != std::string::npos) {
            aggregates.back()._distinct = true;
          } else {
            aggregates.back()._distinct = false;
          }
          inVarName = alias._function.substr(varStart + 1,
                                             varStop - varStart - 1);
          if (aggregates.back()._distinct) {
            inVarName = ad_utility::strip(inVarName, " \t").substr(8);
          }
        }
      }
      inVarName = ad_utility::strip(inVarName, " \t");
      auto inIt = subtreeVarCols.find(inVarName);
      if (inIt == subtreeVarCols.end()) {
        LOG(WARN) << "The aggregate alias " << alias._function << " refers to "
                  << "a column not present in the query." << std::endl;
        if (result->_nofColumns == 1) {
          result->_fixedSizeData = new vector<array<Id, 1>>();
        } else if (result->_nofColumns == 2) {
          result->_fixedSizeData = new vector<array<Id, 2>>();
        } else if (result->_nofColumns == 3) {
          result->_fixedSizeData = new vector<array<Id, 3>>();
        } else if (result->_nofColumns == 4) {
          result->_fixedSizeData = new vector<array<Id, 4>>();
        } else if (result->_nofColumns == 5) {
          result->_fixedSizeData = new vector<array<Id, 5>>();
        }
        result->finish();
        return;
      }
      aggregates.back()._inCol = inIt->second;
      aggregates.back()._outCol = _varColMap.find(alias._outVarName)->second;

    }
  }

  std::shared_ptr<const ResultTable> subresult = _subtree->getResult();

  // populate the result type vector
  result->_resultTypes.resize(result->_nofColumns);
  for (size_t i = 0; i < result->_nofColumns; i++) {
    switch (aggregates[i]._type) {
    case AggregateType::AVG:
      result->_resultTypes[i] = ResultTable::ResultType::FLOAT;
      break;
    case AggregateType::COUNT:
      result->_resultTypes[i] = ResultTable::ResultType::VERBATIM;
      break;
    case AggregateType::GROUP_CONCAT:
      result->_resultTypes[i] = ResultTable::ResultType::STRING;
      break;
    case AggregateType::MAX:
      result->_resultTypes[i] = subresult->getResultType(aggregates[i]._inCol);
      break;
    case AggregateType::MIN:
      result->_resultTypes[i] = subresult->getResultType(aggregates[i]._inCol);
      break;
    case AggregateType::SAMPLE:
      result->_resultTypes[i] = subresult->getResultType(aggregates[i]._inCol);
      break;
    case AggregateType::SUM:
      result->_resultTypes[i] = ResultTable::ResultType::FLOAT;
      break;
    default:
      result->_resultTypes[i] = ResultTable::ResultType::KB;
    }
  }

  std::vector<size_t> groupByCols;
  groupByCols.reserve(_groupByVariables.size());
  for (const string& var : _groupByVariables) {
    groupByCols.push_back(subtreeVarCols[var]);
  }

  std::vector<ResultTable::ResultType> inputResultTypes;
  inputResultTypes.reserve(subresult->_nofColumns);
  for (size_t i = 0; i < subresult->_nofColumns; i++) {
    inputResultTypes.push_back(subresult->getResultType(i));
  }

  callDoGroupBy<1, 1>::call(subresult->_nofColumns, aggregates.size(), subresult,
                            inputResultTypes, groupByCols, aggregates, result,
                            result,
                            getIndex());

  // Free the user data used by GROUP_CONCAT aggregates.
  for (Aggregate& a : aggregates) {
    if (a._type == AggregateType::GROUP_CONCAT) {
      delete static_cast<std::string*>(a._userData);
    }
  }
  result->finish();
}
