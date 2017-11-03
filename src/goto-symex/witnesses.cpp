#include <goto-symex/witnesses.h>
#include <ac_config.h>
#include <boost/property_tree/ptree.hpp>
#include <fstream>
#include <langapi/languages.h>
#include <util/irep2.h>
#include <boost/date_time/posix_time/posix_time.hpp>

typedef boost::property_tree::ptree xmlnodet;

short int nodet::_id = 0;
short int edget::_id = 0;

xmlnodet grapht::generate_graphml(optionst & options)
{
  xmlnodet graphml_node;
  create_graphml(this->verified_file, graphml_node);

  xmlnodet graph_node;
  if (this->witness_type == grapht::VIOLATION)
    create_violation_graph_node(this->verified_file, options, graph_node);
  else
	create_correctness_graph_node(this->verified_file, options, graph_node);

  for (std::vector<edget>::iterator it = this->edges.begin(); it != this->edges.end(); ++it)
  {
     edget current_edge = (edget)*it;
     xmlnodet from_node_node;
     create_node_node(*current_edge.from_node, from_node_node);
     graph_node.add_child("node", from_node_node);
     xmlnodet to_node_node;
     create_node_node(*current_edge.to_node, to_node_node);
     graph_node.add_child("node", to_node_node);
     xmlnodet edge_node;
     create_edge_node(current_edge, edge_node);
     graph_node.add_child("edge", edge_node);
  }
  graphml_node.add_child("graphml.graph", graph_node);

  return graphml_node;
}

/* */
int generate_sha1_hash_for_file(const char * path, std::string & output)
{
  FILE * file = fopen(path, "rb");
  if(!file)
    return -1;

  const int bufSize = 32768;
  char * buffer = (char *) alloca(bufSize);

  crypto_hash c;
  int bytesRead = 0;
  while((bytesRead = fread(buffer, 1, bufSize, file)))
    c.ingest(buffer, bytesRead);

  c.fin();
  output = c.to_string();

  fclose(file);
  return 0;
}

int node_count;
int edge_count;

/* */
std::string execute_cmd(const std::string& command)
{
  /* add ./ for linux execution */
  std::string initial = command.substr(0, 1);
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe)
    return "ERROR";
  char buffer[128];
  std::string result;
  while (!feof(pipe))
  {
    if (fgets(buffer, 128, pipe) != nullptr)
      result += buffer;
  }
  pclose(pipe);
  return result;
}

/* */
std::string read_file(const std::string& path)
{
  std::ifstream t(path.c_str());
  std::string str((std::istreambuf_iterator<char>(t)),
      std::istreambuf_iterator<char>());
  return str;
}

/* */
std::string trim(const std::string& str)
{
  const std::string whitespace_characters = " \t\r\n";
  size_t first_non_whitespace = str.find_first_not_of(whitespace_characters);
  if (first_non_whitespace == std::string::npos)
    return "";
  size_t last_non_whitespace = str.find_last_not_of(whitespace_characters);
  size_t length = last_non_whitespace - first_non_whitespace + 1;
  return str.substr(first_non_whitespace, length);
}

/* */
void map_line_number_to_content(const std::string& source_code_file,
    std::map<int, std::string> & line_content_map)
{
  std::ifstream sfile(source_code_file);
  if (!sfile)
  {
    return;
  }
  std::string source_content = read_file(source_code_file);
  std::istringstream source_stream(source_content);
  std::string line;
  int line_count = 0;
  while (std::getline(source_stream, line))
  {
    line_count++;
    line_content_map[line_count] = trim(line);
  }
}

/* */
void create_node_node(
  nodet & node,
  xmlnodet & nodenode)
{
  nodenode.add("<xmlattr>.id", "n" + std::to_string(node_count++));
  if (node.violation)
  {
    xmlnodet data_violation;
    data_violation.add("<xmlattr>.key", "violation");
    data_violation.put_value("true");
    nodenode.add_child("data", data_violation);
  }
  if (node.sink)
  {
    xmlnodet data_sink;
    data_sink.add("<xmlattr>.key", "sink");
    data_sink.put_value("true");
    nodenode.add_child("data", data_sink);
  }
  if (node.entry)
  {
    xmlnodet data_entry;
    data_entry.add("<xmlattr>.key", "entry");
    data_entry.put_value("true");
    nodenode.add_child("data", data_entry);
  }
  if (node.invariant != 0xFF)
  {
    xmlnodet data_invariant;
    data_invariant.add("<xmlattr>.key", "invariant");
    data_invariant.put_value(node.invariant);
    nodenode.add_child("data", data_invariant);
  }
  if (!node.invariant_scope.empty())
  {
    xmlnodet data_invariant;
    data_invariant.add("<xmlattr>.key", "invariant.scope");
    data_invariant.put_value(node.invariant_scope);
    nodenode.add_child("data", data_invariant);
  }
}

/* */
void create_edge_node(edget & edge, xmlnodet & edgenode)
{
  edgenode.add("<xmlattr>.id", edge.id);
  edgenode.add("<xmlattr>.source", edge.from_node->id);
  edgenode.add("<xmlattr>.target", edge.to_node->id);
  if (edge.start_line != c_nonset)
  {
    xmlnodet data_lineNumberInOrigin;
    data_lineNumberInOrigin.add("<xmlattr>.key", "startline");
    data_lineNumberInOrigin.put_value(edge.start_line);
    edgenode.add_child("data", data_lineNumberInOrigin);
  }
  if (edge.end_line != c_nonset)
  {
    xmlnodet data_endLine;
    data_endLine.add("<xmlattr>.key", "endline");
    data_endLine.put_value(edge.end_line);
    edgenode.add_child("data", data_endLine);
  }
  if (edge.start_offset != c_nonset)
  {
    xmlnodet data_startoffset;
    data_startoffset.add("<xmlattr>.key", "startoffset");
    data_startoffset.put_value(edge.start_offset);
    edgenode.add_child("data", data_startoffset);
  }
  if (edge.end_offset != c_nonset)
  {
    xmlnodet data_endoffset;
    data_endoffset.add("<xmlattr>.key", "endoffset");
    data_endoffset.put_value(edge.end_offset);
    edgenode.add_child("data", data_endoffset);
  }
  if (!edge.return_from_function.empty())
  {
    xmlnodet data_returnFromFunction;
    data_returnFromFunction.add("<xmlattr>.key", "returnFromFunction");
    data_returnFromFunction.put_value(edge.return_from_function);
    edgenode.add_child("data", data_returnFromFunction);
  }
  if (!edge.enter_function.empty())
  {
    xmlnodet data_enterFunction;
    data_enterFunction.add("<xmlattr>.key", "enterFunction");
    data_enterFunction.put_value(edge.enter_function);
    edgenode.add_child("data", data_enterFunction);
  }
  if (!edge.assumption.empty())
  {
    xmlnodet data_assumption;
    data_assumption.add("<xmlattr>.key", "assumption");
    data_assumption.put_value(edge.assumption);
    edgenode.add_child("data", data_assumption);
  }
  if (!edge.assumption_scope.empty())
  {
    xmlnodet data_assumptionScope;
    data_assumptionScope.add("<xmlattr>.key", "assumption.scope");
    data_assumptionScope.put_value(edge.assumption_scope);
    edgenode.add_child("data", data_assumptionScope);
  }
}

/* */
void create_graphml(const std::string& file_path, xmlnodet & graphml)
{
  graphml.add("graphml.<xmlattr>.xmlns",
    "http://graphml.graphdrawing.org/xmlns");
  graphml.add("graphml.<xmlattr>.xmlns:xsi",
    "http://www.w3.org/2001/XMLSchema-instance");

  xmlnodet origin_file_node;
  origin_file_node.add("<xmlattr>.id", "originfile");
  origin_file_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "originFileName");
  origin_file_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  origin_file_node.add("<xmlattr>.for", "edge");
  xmlnodet origin_file_default_node;
  origin_file_default_node.put_value(file_path);
  origin_file_node.add_child("default", origin_file_default_node);
  graphml.add_child("graphml.key", origin_file_node);

  xmlnodet frontier_node;
  frontier_node.add("<xmlattr>.id", "frontier");
  frontier_node.put(
  xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "isFrontierNode");
  frontier_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "boolean");
  frontier_node.add("<xmlattr>.for", "node");
  xmlnodet frontier_default_node;
  frontier_default_node.put_value("false");
  frontier_node.add_child("default", frontier_default_node);
  graphml.add_child("graphml.key", frontier_node);

  xmlnodet violation_node;
  violation_node.add("<xmlattr>.id", "violation");
  violation_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "isViolationNode");
  violation_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "boolean");
  violation_node.add("<xmlattr>.for", "node");
  xmlnodet violation_default_node;
  violation_default_node.put_value("false");
  violation_node.add_child("default", violation_default_node);
  graphml.add_child("graphml.key", violation_node);

  xmlnodet entry_node;
  entry_node.add("<xmlattr>.id", "entry");
  entry_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "isEntryNode");
  entry_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "boolean");
  entry_node.add("<xmlattr>.for", "node");
  xmlnodet entry_default_node;
  entry_default_node.put_value("false");
  entry_node.add_child("default", entry_default_node);
  graphml.add_child("graphml.key", entry_node);

  xmlnodet sink_node;
  sink_node.add("<xmlattr>.id", "sink");
  sink_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "isSinkNode");
  sink_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "boolean");
  sink_node.add("<xmlattr>.for", "node");
  xmlnodet sink_default_node;
  sink_default_node.put_value("false");
  sink_node.add_child("default", sink_default_node);
  graphml.add_child("graphml.key", sink_node);

  xmlnodet source_code_lang_node;
  source_code_lang_node.add("<xmlattr>.id", "sourcecodelang");
  source_code_lang_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "sourcecodeLanguage");
  source_code_lang_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  source_code_lang_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", source_code_lang_node);

  xmlnodet program_file_node;
  program_file_node.add("<xmlattr>.id", "programfile");
  program_file_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "programfile");
  program_file_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  program_file_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", program_file_node);

  xmlnodet program_hash_node;
  program_hash_node.add("<xmlattr>.id", "programhash");
  program_hash_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "programhash");
  program_hash_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  program_hash_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", program_hash_node);

  xmlnodet creation_time_node;
  creation_time_node.add("<xmlattr>.id", "creationtime");
  creation_time_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "creationtime");
  creation_time_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  creation_time_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", creation_time_node);

  xmlnodet specification_node;
  specification_node.add("<xmlattr>.id", "specification");
  specification_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "specification");
  specification_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  specification_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", specification_node);

  xmlnodet architecture_node;
  architecture_node.add("<xmlattr>.id", "architecture");
  architecture_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "architecture");
  architecture_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  architecture_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", architecture_node);

  xmlnodet producer_node;
  producer_node.add("<xmlattr>.id", "producer");
  producer_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "producer");
  producer_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  producer_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", producer_node);

  xmlnodet source_code_node;
  source_code_node.add("<xmlattr>.id", "sourcecode");
  source_code_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "sourcecode");
  source_code_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  source_code_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", source_code_node);

  xmlnodet start_line_node;
  start_line_node.add("<xmlattr>.id", "startline");
  start_line_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "startline");
  start_line_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "int");
  start_line_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", start_line_node);

  xmlnodet start_offset_node;
  start_offset_node.add("<xmlattr>.id", "startoffset");
  start_offset_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "startoffset");
  start_offset_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "int");
  start_offset_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", start_offset_node);

  xmlnodet control_node;
  control_node.add("<xmlattr>.id", "control");
  control_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "control");
  control_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  control_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", control_node);

  xmlnodet invariant_node;
  invariant_node.add("<xmlattr>.id", "invariant");
  invariant_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "invariant");
  invariant_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  invariant_node.add("<xmlattr>.for", "node");
  graphml.add_child("graphml.key", invariant_node);

  xmlnodet invariant_scope_node;
  invariant_scope_node.add("<xmlattr>.id", "invariant.scope");
  invariant_scope_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "invariant.scope");
  invariant_scope_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  invariant_scope_node.add("<xmlattr>.for", "node");
  graphml.add_child("graphml.key", invariant_scope_node);

  xmlnodet assumption_node;
  assumption_node.add("<xmlattr>.id", "assumption");
  assumption_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "assumption");
  assumption_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  assumption_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", assumption_node);

  xmlnodet assumption_scope_node;
  assumption_scope_node.add("<xmlattr>.id", "assumption.scope");
  assumption_scope_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "assumption");
  assumption_scope_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  assumption_scope_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", assumption_scope_node);

  xmlnodet assumption_result_function_node;
  assumption_result_function_node.add("<xmlattr>.id", "assumption.resultfunction");
  assumption_result_function_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "assumption.resultfunction");
  assumption_result_function_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  assumption_result_function_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", assumption_result_function_node);

  xmlnodet enter_function_node;
  enter_function_node.add("<xmlattr>.id", "enterFunction");
  enter_function_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "enterFunction");
  enter_function_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  enter_function_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", enter_function_node);

  xmlnodet return_from_function_node;
  return_from_function_node.add("<xmlattr>.id", "returnFromFunction");
  return_from_function_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "returnFromFunction");
  return_from_function_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  return_from_function_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", return_from_function_node);

  xmlnodet end_line_node;
  end_line_node.add("<xmlattr>.id", "endline");
  end_line_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "endline");
  end_line_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "int");
  end_line_node.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", end_line_node);

  xmlnodet end_offset;
  end_offset.add("<xmlattr>.id", "endoffset");
  end_offset.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "endoffset");
  end_offset.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "int");
  end_offset.add("<xmlattr>.for", "edge");
  graphml.add_child("graphml.key", end_offset);

  xmlnodet witness_type_node;
  witness_type_node.add("<xmlattr>.id", "witness-type");
  witness_type_node.put(
    xmlnodet::path_type("<xmlattr>|attr.name", '|'),
    "witness-type");
  witness_type_node.put(
    xmlnodet::path_type("<xmlattr>|attr.type", '|'),
    "string");
  witness_type_node.add("<xmlattr>.for", "graph");
  graphml.add_child("graphml.key", witness_type_node);
}

/* */
void _create_graph_node(
  std::string & verifiedfile,
  optionst & options,
  xmlnodet & graphnode )
 {
  graphnode.add("<xmlattr>.edgedefault", "directed");

  xmlnodet pProducer;
  pProducer.add("<xmlattr>.key", "producer");
  pProducer.put_value("ESBMC " + std::string(ESBMC_VERSION));
  graphnode.add_child("data", pProducer);

  xmlnodet pSourceCodeLang;
  pSourceCodeLang.add("<xmlattr>.key", "sourcecodelang");
  pSourceCodeLang.put_value("C");
  graphnode.add_child("data", pSourceCodeLang);

  xmlnodet pArchitecture;
  pArchitecture.add("<xmlattr>.key", "architecture");
  pArchitecture.put_value(std::to_string(config.ansi_c.word_size) + "bit");
  graphnode.add_child("data", pArchitecture);

  xmlnodet pProgramFile;
  pProgramFile.add("<xmlattr>.key", "programfile");
  pProgramFile.put_value(verifiedfile);
  graphnode.add_child("data", pProgramFile);

  std::string programFileHash;
  if (!verifiedfile.empty())
    generate_sha1_hash_for_file(verifiedfile.c_str(), programFileHash);
  xmlnodet pProgramHash;
  pProgramHash.add("<xmlattr>.key", "programhash");
  pProgramHash.put_value(programFileHash);
  graphnode.add_child("data", pProgramHash);

  xmlnodet pDataSpecification;
  pDataSpecification.add("<xmlattr>.key", "specification");
  if (options.get_bool_option("overflow-check"))
    pDataSpecification.put_value("CHECK( init(main()), LTL(G ! overflow) )");
  else if (options.get_bool_option("memory-leak-check"))
    pDataSpecification.put_value("CHECK( init(main()), LTL(G valid-free|valid-deref|valid-memtrack) )");
  else
    pDataSpecification.put_value("CHECK( init(main()), LTL(G ! call(__VERIFIER_error())) )");
  graphnode.add_child("data", pDataSpecification);

  boost::posix_time::ptime creation_time = boost::posix_time::microsec_clock::universal_time();
  xmlnodet p_creationTime;
  p_creationTime.add("<xmlattr>.key", "creationtime");
  p_creationTime.put_value(boost::posix_time::to_iso_extended_string(creation_time));
  graphnode.add_child("data", p_creationTime);
}

/* */
void create_violation_graph_node(
  std::string & verifiedfile,
  optionst & options,
  xmlnodet & graphnode )
{
  _create_graph_node(verifiedfile, options, graphnode);
  xmlnodet pWitnessType;
  pWitnessType.add("<xmlattr>.key", "witness-type");
  pWitnessType.put_value("violation_witness");
  graphnode.add_child("data", pWitnessType);
}

/* */
void create_correctness_graph_node(
  std::string & verifiedfile,
  optionst & options,
  xmlnodet & graphnode )
{
  _create_graph_node(verifiedfile, options, graphnode);
  xmlnodet pWitnessType;
  pWitnessType.add("<xmlattr>.key", "witness-type");
  pWitnessType.put_value("correctness_witness");
  graphnode.add_child("data", pWitnessType);
}

std::string get_formated_assignment(const namespacet & ns, const goto_trace_stept & step)
{
  const irep_idt &identifier = to_symbol2t(step.original_lhs).get_symbol_name();
  std::string lhs_symbol = id2string(identifier);
  const symbolt *symbol;
  if(!ns.lookup(identifier, symbol) && !symbol->pretty_name.empty())
    lhs_symbol = id2string(symbol->pretty_name);
  std::vector<std::string> id_sections;
  boost::split(id_sections, lhs_symbol, boost::is_any_of("::"));
  lhs_symbol = id_sections[id_sections.size()-1];
  std::string rhs_value = from_expr(ns, identifier, step.value);
  rhs_value = std::regex_replace (rhs_value, std::regex("f"),"");
  return lhs_symbol + "=" + rhs_value + ";";
}

/* */
std::string w_string_replace(
  std::string subject,
  const std::string & search,
  const std::string & replace)
{
  size_t pos = 0;
  while ((pos = subject.find(search, pos)) != std::string::npos) {
     subject.replace(pos, search.length(), replace);
     pos += replace.length();
  }
  return subject;
}

/* */
void get_offsets_for_line_using_wc(
  const std::string & file_path,
  const uint16_t line_number,
  uint16_t & p_startoffset,
  uint16_t & p_endoffset)
{
  uint16_t startoffset = 0;
  uint16_t endoffset = 0;

  try {
    /* get the offsets */
    startoffset = std::atoi(execute_cmd("cat " + file_path + " | head -n " + std::to_string(line_number - 1) + " | wc --chars").c_str());
    endoffset = std::atoi(execute_cmd("cat " + file_path + " | head -n " + std::to_string(line_number) + " | wc --chars").c_str());
    /* count the spaces in the beginning and append to the startoffset  */
    std::string str_line = execute_cmd("cat " + file_path + " | head -n " + std::to_string(line_number) + " | tail -n 1 ");
    uint16_t i=0;
    for (i=0; i<str_line.length(); i++)
    {
      if (str_line.c_str()[i] == ' ')
        startoffset++;
      else
        break;
    }
  } catch (const std::exception& e) {
    /* nothing to do here */
  }

  p_startoffset = startoffset;
  p_endoffset = endoffset;
}

/* */
bool is_valid_witness_step(
  const namespacet &ns,
  const goto_trace_stept & step)
{
  languagest languages(ns, "C");
  std::string lhsexpr;
  languages.from_expr(migrate_expr_back(step.lhs), lhsexpr);
  std::string location = step.pc->location.to_string();
  return ((location.find("built-in") & location.find("library") &
           lhsexpr.find("__ESBMC") & lhsexpr.find("stdin") &
           lhsexpr.find("stdout") & lhsexpr.find("stderr") &
           lhsexpr.find("sys_")) == std::string::npos);
}

/* */
bool is_valid_witness_expr(
  const namespacet &ns,
	const irep_container<expr2t> & exp)
{
  languagest languages(ns, "C");
  std::string value;
  languages.from_expr(migrate_expr_back(exp), value);
  return (value.find("__ESBMC") &
    value.find("stdin")         &
    value.find("stdout")        &
    value.find("stderr")        &
    value.find("sys_")) == std::string::npos;
}

/* */
void get_relative_line_in_programfile(
  const std::string& relative_file_path,
  const int relative_line_number,
  const std::string& program_file_path,
  int & programfile_line_number)
{
  /* check if it is necessary to get the relative line */
  if (relative_file_path == program_file_path)
  {
	programfile_line_number = relative_line_number;
    return;
  }
  std::string line;
  std::string relative_content;
  std::ifstream stream_relative (relative_file_path);
  std::ifstream stream_programfile (program_file_path);
  int line_count = 0;
  /* get the relative content */
  if (stream_relative.is_open())
  {
	while(getline(stream_relative, line) &&
		  line_count < relative_line_number)
	{
	  relative_content = line;
	  line_count++;
	}
  }

  /* file for the line in the programfile */
  line_count = 1;
  if (stream_programfile.is_open())
  {
    while(getline(stream_programfile, line) &&
  	  line != relative_content)
    {
      line_count++;
    }
  }
  programfile_line_number = line_count;
}
