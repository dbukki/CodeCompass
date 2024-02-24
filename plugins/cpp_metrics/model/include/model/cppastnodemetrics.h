#ifndef CC_MODEL_CPPASTNODEMETRICS_H
#define CC_MODEL_CPPASTNODEMETRICS_H

#include <model/cppastnode.h>
#include <model/cppentity.h>
#include <model/cpprecord.h>

namespace cc
{
namespace model
{

#pragma db object
struct CppAstNodeMetrics
{
  enum Type
  {
    PARAMETER_COUNT,
    MCCABE,
    BUMPY_ROAD,
    LACK_OF_COHESION,
    LACK_OF_COHESION_HS,
  };

  #pragma db id auto
  std::uint64_t id;

  #pragma db not_null
  CppAstNodeId astNodeId;

  #pragma db not_null
  Type type;

  #pragma db null
  double value;
};

#pragma db view \
  object(CppRecord) \
  object(CppAstNodeMetrics : \
    CppRecord::astNodeId == CppAstNodeMetrics::astNodeId)
struct CppRecordMetricsView
{
  #pragma db column(CppAstNodeMetrics::value)
  double value;
};

} //model
} //cc

#endif //CC_MODEL_CPPASTNODEMETRICS_H
