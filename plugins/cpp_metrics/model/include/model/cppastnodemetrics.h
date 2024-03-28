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
    PARAMETER_COUNT = 1,
    MCCABE = 2,
    BUMPY_ROAD = 3,
    LACK_OF_COHESION = 4,
    LACK_OF_COHESION_HS = 5,
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

#pragma db view \
  object(CppAstNodeMetrics) \
  object(CppAstNode : CppAstNodeMetrics::astNodeId == CppAstNode::id) \
  object(File : CppAstNode::location.file) \
  object(CppEntity : CppAstNode::id == CppEntity::astNodeId)
struct CppMetricsLocationView
{
  //#pragma db column(CppEntity::name)
  //std::string name;
  #pragma db column(CppEntity::qualifiedName)
  std::string qualifiedName;

  typedef cc::model::CppAstNodeMetrics::Type Type;

  #pragma db column(CppAstNodeMetrics::type)
  Type type;
  #pragma db column(CppAstNodeMetrics::value)
  double value;

  typedef cc::model::Position::PosType PosType;

  #pragma db column(CppAstNode::location.range.start.line)
  PosType startLine;
  #pragma db column(CppAstNode::location.range.start.column)
  PosType startColumn;
  #pragma db column(CppAstNode::location.range.end.line)
  PosType endLine;
  #pragma db column(CppAstNode::location.range.end.column)
  PosType endColumn;

  #pragma db column(File::path)
  std::string filePath;
};

} //model
} //cc

#endif //CC_MODEL_CPPASTNODEMETRICS_H
