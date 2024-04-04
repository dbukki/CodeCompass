#include <cppmetricsparser/cppmetricsparser.h>

#include <model/cppastnodemetrics.h>
#include <model/cppastnodemetrics-odb.hxx>
#include <model/cppcohesionmetrics.h>
#include <model/cppcohesionmetrics-odb.hxx>
#include <model/cppfilemetrics.h>
#include <model/cppfilemetrics-odb.hxx>

#include <model/cppastnode.h>
#include <model/cppastnode-odb.hxx>

#include <boost/filesystem.hpp>

#include <util/filesystem.h>
#include <util/logutil.h>
#include <util/filesystem.h>
#include <util/odbtransaction.h>

#include <memory>
#include <iostream>
#include <fstream>

namespace cc
{
namespace parser
{

namespace fs = boost::filesystem;

CppMetricsParser::CppMetricsParser(ParserContext& ctx_): AbstractParser(ctx_)
{
  for (const std::string& path :
    _ctx.options["input"].as<std::vector<std::string>>())
    _inputPaths.push_back(fs::canonical(path).string());

  util::OdbTransaction {_ctx.db} ([&, this] {
    for (const model::CppFileMetrics& fm
      : _ctx.db->query<model::CppFileMetrics>())
    {
      _fileIdCache.insert(fm.file);
    }

    for (const model::CppAstNodeMetrics& anm
      : _ctx.db->query<model::CppAstNodeMetrics>())
    {
      auto node = _ctx.db->query_one<model::CppAstNode>(
        odb::query<model::CppAstNode>::id == anm.astNodeId);
      _astNodeIdCache.insert({anm.astNodeId, node->location.file->id});
    }
  });
}

bool CppMetricsParser::cleanupDatabase()
{
  if (!_fileIdCache.empty())
  {
    try
    {
      util::OdbTransaction {_ctx.db} ([this] {
        for (const model::File& file
          : _ctx.db->query<model::File>(
          odb::query<model::File>::id.in_range(_fileIdCache.begin(), _fileIdCache.end())))
        {
          auto it = _ctx.fileStatus.find(file.path);
          if (it != _ctx.fileStatus.end() &&
              (it->second == cc::parser::IncrementalStatus::DELETED ||
               it->second == cc::parser::IncrementalStatus::MODIFIED ||
               it->second == cc::parser::IncrementalStatus::ACTION_CHANGED))
          {
            LOG(info) << "[cxxmetricsparser] Database cleanup: " << file.path;

            _ctx.db->erase_query<model::CppFileMetrics>(odb::query<model::CppFileMetrics>::file == file.id);
            _fileIdCache.erase(file.id);
          }
        }

        for (const auto& pair : _astNodeIdCache)
        {
          auto file = _ctx.db->query_one<model::File>(
            odb::query<model::File>::id == pair.second);

          auto it = _ctx.fileStatus.find(file->path);
          if (it != _ctx.fileStatus.end() &&
              (it->second == cc::parser::IncrementalStatus::DELETED ||
               it->second == cc::parser::IncrementalStatus::MODIFIED ||
               it->second == cc::parser::IncrementalStatus::ACTION_CHANGED))
          {
            LOG(info) << "[cxxmetricsparser] Database cleanup: " << file->path;

            _ctx.db->erase_query<model::CppAstNodeMetrics>(odb::query<model::CppAstNodeMetrics>::astNodeId == pair.first);
            _astNodeIdCache.erase(pair.first);
          }
        }
      });
    }
    catch (odb::database_exception&)
    {
      LOG(fatal) << "Transaction failed in cxxmetrics parser!";
      return false;
    }
  }
  return true;
}


void CppMetricsParser::linesOfCode()
{
  util::OdbTransaction {_ctx.db} ([&, this]
  {
    for (const model::CppFunctionLOC& loc
      : _ctx.db->query<model::CppFunctionLOC>())
    {
      // Skip functions that were included from external libraries.
      if (!cc::util::isRootedUnderAnyOf(_inputPaths, loc.filePath))
        continue;

      model::CppAstNodeMetrics metric;
      metric.astNodeId = loc.id;
      metric.type = model::CppAstNodeMetrics::Type::LINES_OF_CODE;
      metric.value = loc.lines;
      _ctx.db->persist(metric);
    }
  });
}

void CppMetricsParser::functionParameters()
{
  util::OdbTransaction {_ctx.db} ([&, this]
  {
    for (const model::CppFunctionParamCountWithId& paramCount
      : _ctx.db->query<model::CppFunctionParamCountWithId>())
    {
      // Skip functions that were included from external libraries.
      if (!cc::util::isRootedUnderAnyOf(_inputPaths, paramCount.filePath))
        continue;

      model::CppAstNodeMetrics funcParams;
      funcParams.astNodeId = paramCount.id;
      funcParams.type = model::CppAstNodeMetrics::Type::PARAMETER_COUNT;
      funcParams.value = paramCount.count;
      _ctx.db->persist(funcParams);
    }
  });
}

void CppMetricsParser::functionLocals()
{
  util::OdbTransaction {_ctx.db} ([&, this]
  {
    for (const model::CppFunctionLocalCountWithId& localCount
      : _ctx.db->query<model::CppFunctionLocalCountWithId>())
    {
      // Skip functions that were included from external libraries.
      if (!cc::util::isRootedUnderAnyOf(_inputPaths, localCount.filePath))
        continue;

      model::CppAstNodeMetrics funcLocals;
      funcLocals.astNodeId = localCount.id;
      funcLocals.type = model::CppAstNodeMetrics::Type::LOCAL_COUNT;
      funcLocals.value = localCount.count;
      _ctx.db->persist(funcLocals);
    }
  });
}

void CppMetricsParser::functionMcCabe()
{
  util::OdbTransaction {_ctx.db} ([&, this]
  {
    typedef odb::query<model::CppFunctionMcCabe>::query_columns QFun;
    const auto& QFunDef = QFun::CppFunction::isDefinition;
    
    for (const model::CppFunctionMcCabe& function
      : _ctx.db->query<model::CppFunctionMcCabe>(QFunDef == true))
    {
      // Skip functions that were included from external libraries.
      if (!cc::util::isRootedUnderAnyOf(_inputPaths, function.filePath))
        continue;

      model::CppAstNodeMetrics funcBC;
      funcBC.astNodeId = function.astNodeId;
      funcBC.type = model::CppAstNodeMetrics::Type::BRANCH_COUNT;
      funcBC.value = function.branchCount;
      _ctx.db->persist(funcBC);

      model::CppAstNodeMetrics funcLC;
      funcLC.astNodeId = function.astNodeId;
      funcLC.type = model::CppAstNodeMetrics::Type::LOOP_COUNT;
      funcLC.value = function.loopCount;
      _ctx.db->persist(funcLC);

      model::CppAstNodeMetrics funcFC;
      funcFC.astNodeId = function.astNodeId;
      funcFC.type = model::CppAstNodeMetrics::Type::FLOW_COUNT;
      funcFC.value = function.flowCount;
      _ctx.db->persist(funcFC);

      model::CppAstNodeMetrics funcMcCabe;
      funcMcCabe.astNodeId = function.astNodeId;
      funcMcCabe.type = model::CppAstNodeMetrics::Type::MCCABE;
      funcMcCabe.value = 1 + function.branchCount + function.loopCount + function.flowCount;
      _ctx.db->persist(funcMcCabe);
    }
  });
}

void CppMetricsParser::functionBumpyRoad()
{
  util::OdbTransaction {_ctx.db} ([&, this]
  {
    typedef odb::query<model::CppFunctionBumpyRoad>::query_columns QFun;
    const auto& QFunDef = QFun::CppFunction::isDefinition;
    
    for (const model::CppFunctionBumpyRoad& function
      : _ctx.db->query<model::CppFunctionBumpyRoad>(QFunDef == true))
    {
      // Skip functions that were included from external libraries.
      if (!cc::util::isRootedUnderAnyOf(_inputPaths, function.filePath))
        continue;

      const double dB = function.bumpiness;
      const double dC = function.statementCount;
      const bool empty = function.statementCount == 0;

      model::CppAstNodeMetrics br;
      br.astNodeId = function.astNodeId;
      br.type = model::CppAstNodeMetrics::Type::BUMPY_ROAD;
      br.value = empty ? 1.0 : (dB / dC);
      _ctx.db->persist(br);

      model::CppAstNodeMetrics sc;
      sc.astNodeId = function.astNodeId;
      sc.type = model::CppAstNodeMetrics::Type::STATEMENT_COUNT;
      sc.value = function.statementCount;
      _ctx.db->persist(sc);

      model::CppAstNodeMetrics nn;
      nn.astNodeId = function.astNodeId;
      nn.type = model::CppAstNodeMetrics::Type::NESTEDNESS;
      nn.value = function.nestedness;
      _ctx.db->persist(nn);
    }
  });
}

void CppMetricsParser::lackOfCohesion()
{
  util::OdbTransaction {_ctx.db} ([&, this]
  {
    // Simplify some type names for readability.
    typedef std::uint64_t HashType;

    typedef odb::query<model::CohesionCppFieldView>::query_columns QField;
    const auto& QFieldTypeHash = QField::CppMemberType::typeHash;

    typedef odb::query<model::CohesionCppMethodView>::query_columns QMethod;
    const auto& QMethodTypeHash = QMethod::CppMemberType::typeHash;
    
    typedef odb::query<model::CohesionCppAstNodeView>::query_columns QNode;
    const auto& QNodeFilePath = QNode::File::path;
    const auto& QNodeRange = QNode::CppAstNode::location.range;

    // Calculate the cohesion metric for all types.
    for (const model::CohesionCppRecordView& type
      : _ctx.db->query<model::CohesionCppRecordView>())
    {
      // Skip types that were included from external libraries.
      if (!cc::util::isRootedUnderAnyOf(_inputPaths, type.filePath))
        continue;

      std::unordered_set<HashType> fieldHashes;
      // Query all fields of the current type.
      for (const model::CohesionCppFieldView& field
        : _ctx.db->query<model::CohesionCppFieldView>(
          QFieldTypeHash == type.entityHash
        ))
      {
        // Record these fields for later use.
        fieldHashes.insert(field.entityHash);
      }
      std::size_t fieldCount = fieldHashes.size();

      std::size_t methodCount = 0;
      std::size_t totalCohesion = 0;
      // Query all methods of the current type.
      for (const model::CohesionCppMethodView& method
        : _ctx.db->query<model::CohesionCppMethodView>(
          QMethodTypeHash == type.entityHash
        ))
      {
        // Do not consider methods with no explicit bodies.
        const model::Position start(method.startLine, method.startColumn);
        const model::Position end(method.endLine, method.endColumn);
        if (start < end)
        {
          std::unordered_set<HashType> usedFields;
          
          // Query all AST nodes that use a variable for reading or writing...
          for (const model::CohesionCppAstNodeView& node
            : _ctx.db->query<model::CohesionCppAstNodeView>(
              // ... in the same file as the current method
              (QNodeFilePath == method.filePath &&
              // ... within the textual scope of the current method's body.
              (QNodeRange.start.line >= start.line
                || (QNodeRange.start.line == start.line
                && QNodeRange.start.column >= start.column)) &&
              (QNodeRange.end.line <= end.line
                || (QNodeRange.end.line == end.line
                && QNodeRange.end.column <= end.column)))
            ))
          {
            // If this AST node is a reference to a field of the type...
            if (fieldHashes.find(node.entityHash) != fieldHashes.end())
            {
              // ... then mark it as used by this method.
              usedFields.insert(node.entityHash);
            }
          }
          
          ++methodCount;
          totalCohesion += usedFields.size();
        }
      }

      // Calculate and record metrics.
      const double dF = fieldCount;
      const double dM = methodCount;
      const double dC = totalCohesion;
      const bool trivial = fieldCount == 0 || methodCount == 0;
      const bool singular = methodCount == 1;
      
      // Standard lack of cohesion (range: [0,1])
      model::CppAstNodeMetrics lcm;
      lcm.astNodeId = type.astNodeId;
      lcm.type = model::CppAstNodeMetrics::Type::LACK_OF_COHESION;
      lcm.value = trivial ? 0.0 :
        (1.0 - dC / (dM * dF));
      _ctx.db->persist(lcm);

      // Henderson-Sellers variant (range: [0,2])
      model::CppAstNodeMetrics lcm_hs;
      lcm_hs.astNodeId = type.astNodeId;
      lcm_hs.type = model::CppAstNodeMetrics::Type::LACK_OF_COHESION_HS;
      lcm_hs.value = trivial ? 0.0 : singular ? NAN :
        ((dM - dC / dF) / (dM - 1.0));
      _ctx.db->persist(lcm_hs);
    }
  });
}

class AstNodeLoc
{
public:
  typedef model::Position::PosType Pos;

private:
  std::string _file;
  model::Range _range;
  std::size_t _hash;

public:
  const std::string& file() const { return _file; }
  const model::Range& range() const { return _range; }

public:
  struct Hash
  { std::size_t operator()(const AstNodeLoc& node) const { return node._hash; } };

public:
  AstNodeLoc(const std::string& file_, Pos sLn_, Pos sCol_, Pos eLn_, Pos eCol_) :
    _file(file_),
    _range(model::Position(sLn_, sCol_), model::Position(eLn_, eCol_)),
    _hash(0)
  {
    std::hash<std::string> hStr;
    std::hash<Pos> hPos;
    
    _hash ^= hStr(_file);
    _hash ^= hPos(_range.start.line);
    _hash ^= hPos(_range.start.column);
    _hash ^= hPos(_range.end.line);
    _hash ^= hPos(_range.end.column);
  }

public:
  bool operator==(const AstNodeLoc& other) const
  {
    return _range == other._range && _file == other._file;
  }
};

class MetricsExtractor
{
private:
  cc::parser::ParserContext& _ctx;
  fs::path _root;
  fs::path _code;
  std::unordered_map<AstNodeLoc, std::size_t, AstNodeLoc::Hash> _cache;

public:
  const fs::path& RootDir() const { return _root; }
  const fs::path& CodeDir() const { return _code; }

public:
  MetricsExtractor(cc::parser::ParserContext& ctx_, const fs::path& path_) :
    _ctx(ctx_),
    _root(path_),
    _code(_root / "Code/"),
    _cache()
  {
    fs::create_directories(_code);
    _root = fs::canonical(_root);
    _code = fs::canonical(_code);
  }

  void Extract(model::CppAstNodeMetrics::Type type_, const char* name_)
  {
    util::OdbTransaction {_ctx.db} ([&, this]
    {
      std::ostringstream ossName;
      ossName << name_ << ".ccr";
      std::ofstream ofsReg((_root / ossName.str()).c_str());

      constexpr auto npos = model::Position::npos;

      typedef odb::query<model::CppMetricsLocationView>::query_columns QView;
      const auto& QViewType = QView::CppAstNodeMetrics::type;

      for (const model::CppMetricsLocationView& view
        : _ctx.db->query<model::CppMetricsLocationView>(QViewType == type_))
      {
        if ((view.startLine != npos && view.startColumn != npos
          && view.endLine != npos && view.endColumn != npos)
          && (view.startLine < view.endLine
          || (view.startLine == view.endLine && view.startColumn < view.endColumn)))
        {
          AstNodeLoc loc(view.filePath,
            view.startLine, view.startColumn,
            view.endLine, view.endColumn);

          auto resCode = _cache.insert(std::make_pair(loc, _cache.size()));
          if (resCode.second)
          {
            ossName.str("");
            ossName << resCode.first->second << ".ccx";
            std::ifstream isSrc(loc.file());
            std::ofstream osDst((_code / ossName.str()).c_str());

            AstNodeLoc::Pos ln  = 1;
            AstNodeLoc::Pos col = 1;
            bool extract = false;
            int ch;
            while ((ch = isSrc.get()) >= 0)
            {
              if (ln == loc.range().start.line && col == loc.range().start.column)
                extract = true;
              if (ln == loc.range().end.line && col == loc.range().end.column)
                { extract = false; break; }

              if (extract)
                osDst.put(ch);

              if (ch == '\n')
                ++ln, col = 1;
              else ++col;
            }

            if (extract)
            {
              std::cerr << "Error while extracting:\n\t"
                << loc.file()
                << '\t' << loc.range().start.line << ':' << loc.range().start.column
                << '-'  << loc.range().end.line   << ':' << loc.range().end.column
                << "\n\tln = " << ln << ", col = " << col
                << std::endl;
              assert(false);
            }
          }

          ofsReg << view.qualifiedName << '\t'
                 << view.value << '\t'
                 << resCode.first->second << '\n';
        }
      }
    });
  }

};

bool CppMetricsParser::parse()
{
  linesOfCode();
  //functionParameters();
  functionLocals();
  functionMcCabe();
  functionBumpyRoad();
  //lackOfCohesion();

  extract();
  return true;
}

bool CppMetricsParser::extract()
{
  std::string sExPath;
  const auto& varExPath = _ctx.options["extract-to"];
  if (varExPath.empty())
  {
    std::cout << "\nExtraction path: ";
    std::getline(std::cin, sExPath);
  }
  else
  {
    sExPath = varExPath.as<std::string>();
  }

  if (!sExPath.empty())
  {
    MetricsExtractor me(_ctx, sExPath);
    std::cout << "Extracting to: " << me.RootDir().c_str() << std::endl;

    typedef model::CppAstNodeMetrics::Type Type;
    me.Extract(Type::BRANCH_COUNT, "BranchCount");
    me.Extract(Type::LOOP_COUNT, "LoopCount");
    me.Extract(Type::FLOW_COUNT, "FlowCount");
    me.Extract(Type::MCCABE, "McCabe");
    me.Extract(Type::BUMPY_ROAD, "BumpyRoad");
    me.Extract(Type::STATEMENT_COUNT, "StatementCount");
    me.Extract(Type::NESTEDNESS, "Nestedness");
    me.Extract(Type::LINES_OF_CODE, "LinesOfCode");
    me.Extract(Type::LOCAL_COUNT, "LocalCount");

    return true;
  }
  else
  {
    std::cout << "No extraction took place." << std::endl;
    return false;
  }
}

CppMetricsParser::~CppMetricsParser()
{
}

/* These two methods are used by the plugin manager to allow dynamic loading
   of CodeCompass Parser plugins. Clang (>= version 6.0) gives a warning that
   these C-linkage specified methods return types that are not proper from a
   C code.

   These codes are NOT to be called from any C code. The C linkage is used to
   turn off the name mangling so that the dynamic loader can easily find the
   symbol table needed to set the plugin up.
*/
// When writing a plugin, please do NOT copy this notice to your code.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C"
{
  boost::program_options::options_description getOptions()
  {
    boost::program_options::options_description description("C++ Metrics Plugin");
    description.add_options()
      ("extract-to", po::value<std::string>(), "Name of directory to extract metrics data to.");

    return description;
  }

  std::shared_ptr<CppMetricsParser> make(ParserContext& ctx_)
  {
    return std::make_shared<CppMetricsParser>(ctx_);
  }
}
#pragma clang diagnostic pop

} // parser
} // cc
