/*
Copyright (C) 2016 Pawel Gajer pgajer@gmail.com and Jacques Ravel jravel@som.umaryland.edu

Permission to use, copy, modify, and distribute this software and its
documentation with or without modifications and for any purpose and
without fee is hereby granted, provided that any copyright notices
appear in all copies and that both those copyright notices and this
permission notice appear in supporting documentation, and that the
names of the contributors or copyright holders not be used in
advertising or publicity pertaining to distribution of the software
without specific prior permission.

THE CONTRIBUTORS AND COPYRIGHT HOLDERS OF THIS SOFTWARE DISCLAIM ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL THE
CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT
OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
OR PERFORMANCE OF THIS SOFTWARE.
*/

/*
  ToDo's

  modify input parameters making

  -f <ref_dir>

  optional only for the case when sampling is done using ref seq's
  (Currently not used).

*/


/*
  Estimating error probabilities of p(x|M), where x is a query sequence and M is
  an MC model of one of the nodes of the reference tree.

  Outline

  Traverse ref tree and for each node, s, that is not root, do the following.

  1. Identify siblings s_1, ... , s_n of s (say s=s_0).

  2. For each sibling s_i generate a random sample of size, say 1000, sequences
  generated by the MC model M_i associated with s_i.

  3. Count how many times

  p(x|M) = max_{j=0...n} p(x|M_j)

  where M is the model associated with s and x is a random sequence of M_i.

  Let w_i be the proportion of times the above equality holds.

  4. Generate a file with 1002 columns and the number of rows = number of
  siblings of s + 1. The first column contains the name of each node, the second
  w_i, except for the first row which contains 1000 random sequences of M and the
  first column is 0. The last 1000 columns contain random sequences of M_i.

  5. Process in R these files building for each node a probability of
  classification error

  p( p(x|M) > c )

  which is the cummulative distribution function associated with the mixture

  w_i * p(x|M_i) for x random from M_i


  6. Do the cross-validation tests using probability of error stats


  Usage example

  cd /Users/pgajer/projects/16S_rRNA_pipeline/vaginal_species_oct18_2013

  clError -d vaginal_v2_MCdir -f vaginal_v2_dir -r vaginal_v2_dir/refTx.tree -o vaginal_v2_clError_dir

*/


#include <getopt.h>
#include <string.h>
#include <string>
#include <vector>
#include <queue>

#include "CUtilities.h"
#include "IOCUtilities.h"
#include "IOCppUtilities.hh"
#include "CppUtilities.hh"
#include "MarkovChains2.hh"
#include "StatUtilities.hh"
#include "Newick.hh"
#include "CStatUtilities.h"

using namespace std;

//----------------------------------------------------------- printUsage ----
void printUsage( const char *s )
{
  cout << endl

       << "USAGE " << endl
       << endl
       << " Using prebuilt MC models this program generates data necessary to compute probability of classification error" << endl
       << endl
       << s << " -d < MC models directory> -r <ref tree> -o <output directory> [Options]" << endl
       << endl
       << "\tOptions:\n"
       << "\t-d <dir>       - directory containing MC model files\n"
       << "\t-r <ref tree>  - reference tree with node labels corresponding to the names of the model files\n"
       << "\t-f <fasta dir> - directory with reference fasta files\n"
       << "\t-o <dir>       - output directory containg mixture data for each node of the reference tree\n"

       << "\n\tExample: \n"

       << s << " -d vaginal_v2_MCdir -o vaginal_v2_clError_dir" << endl << "OR" << endl

       << s << " -f vaginal_v2_dir -d vaginal_v2_MCdir -r vaginal_v2_dir/refTx.tree -o vaginal_v2_clError_dir" << endl << endl;
}


//----------------------------------------------- printHelp ----
void printHelp( const char *s )
{
    printUsage(s);
}

//================================================= inPar2_t ====
//! holds input parameters
class inPar2_t
{
public:
  inPar2_t();
  ~inPar2_t();

  char *outDir;             /// output directory for MC taxonomy files
  char *mcDir;              /// input directory for MC model files
  char *trgFile;            /// file containing paths to fasta training files
  char *faDir;              /// directory of reference fasta files
  char *inFile;             /// input file with path(s) to fasta file(s) containing sequences
                            /// for which -log10(prob(seq | model_i)) are to be computed
  char *seqID;              /// sequence ID of a sequence from the training fasta files that is to be excluded
                            /// from model building and needs to be used for cross validation
  char *treeFile;           /// reference tree file
  double thld;              /// threshold for | log( p(x | M_L) / p(x | M_R) | of the competing models
  vector<char *> trgFiles;  /// list of paths to fasta training files
  vector<int> kMerLens;     /// list of word lengths
  int printCounts;          /// flag initiating print out of word counts
  int maxNumAmbCodes;       /// maximal acceptable number of ambiguity codes for a sequence; above this number log10probIUPAC() returns 1;
  int randSampleSize;       /// number of random sequences of each model (seq length = mean ref seq). If 0, no random samples will be generated.
  int pseudoCountType;      /// pseudo-count type; see MarkovChains2.hh for possible values
  bool verbose;

  void print();
};

//------------------------------------------------- constructor ----
inPar2_t::inPar2_t()
{
  outDir          = NULL;
  mcDir           = NULL;
  faDir           = NULL;
  trgFile         = NULL;
  inFile          = NULL;
  treeFile        = NULL;
  seqID           = NULL;
  thld            = 0.0;
  printCounts     = 0;
  maxNumAmbCodes  = 5;
  randSampleSize  = 1000;
  pseudoCountType = recPdoCount;
  verbose         = false;
}

//------------------------------------------------- constructor ----
inPar2_t::~inPar2_t()
{
  if ( outDir )
    free(outDir);

  if ( mcDir )
    free(mcDir);

  if ( faDir )
    free(faDir);

  if ( trgFile )
    free(trgFile);

  if ( inFile )
    free(inFile);

  if ( seqID )
    free(seqID);

  if ( treeFile )
    free(treeFile);

  int n = trgFiles.size();
  for ( int i = 0; i < n; ++i )
    free(trgFiles[i]);
}

//------------------------------------------------------- print ----
void inPar2_t::print()
{
  cerr << "printCounts=\t" << printCounts
       << "\npseudoCountType=\t" << pseudoCountType
       << "\nverbose=\t" << verbose
       << "\ntrgFile=\t";

  if ( trgFile )
    cerr << trgFile << endl;
  else
    cerr << endl;

  cerr << "mcDir=\t\t";
  if ( mcDir )
    cerr << mcDir << endl;
  else
    cerr << "MISSING" << endl;

  cerr << "faDir=\t\t";
  if ( faDir )
    cerr << faDir << endl;
  else
    cerr << "MISSING" << endl;

  cerr << "outDir=\t\t";
  if ( outDir )
    cerr << outDir << endl;
  else
    cerr << "MISSING" << endl;

  cerr << "inFile=\t\t";
  if ( inFile )
    cerr << inFile << endl;
  else
    cerr << "MISSING" << endl;

  cerr << "seqID=\t\t";
  if ( seqID )
    cerr << seqID << endl;
  else
    cerr << "MISSING" << endl;

  cerr << "treeFile=\t\t";
  if ( treeFile )
    cerr << treeFile << endl;
  else
    cerr << "MISSING" << endl;

  cerr << "trgFiles:\t";
  int n = trgFiles.size();
  for ( int i = 0; i < n; ++i )
    cerr << trgFiles[i] << "\t";
  cerr << endl;

  cerr << "kMerLens:";
  n = kMerLens.size();
  for ( int i = 0; i < n; ++i )
    cerr << "\t" << kMerLens[i];
  cerr << endl;
}

//============================== local sub-routines =========================
void parseArgs( int argc, char ** argv, inPar2_t *p );
bool dComp (double i, double j) { return (i>j); }

//============================== main ======================================
int main(int argc, char **argv)
{
  int seqLen = 1460; // length of random sequences

  //-- setting up init parameters
  inPar2_t *inPar = new inPar2_t();

  //-- parsing input parameters
  parseArgs(argc, argv, inPar);

  if ( inPar->verbose )
    inPar->print();

  #if 0
  if ( !inPar->faDir )
  {
    fprintf(stderr, "ERROR in %s at line %d: faDir not defined\n", __FILE__, __LINE__);
    exit(1);
  }
  #endif

  NewickTree_t nt;
  if ( inPar->treeFile ) // load ref tree
  {
    if ( !nt.loadTree(inPar->treeFile) )
    {
      fprintf(stderr,"Could not load Newick tree from %s\n", inPar->treeFile);
      exit(EXIT_FAILURE);
    }
  }
  else
  {
    // lets see if we can find ref tree in mcDir
    // refTx.tree
    string trFile = string(inPar->mcDir) + "/refTx.tree";
    STRDUP(inPar->treeFile, trFile.c_str());

    if ( !nt.loadTree(inPar->treeFile) )
    {
      cout << endl << "ERROR in "<< __FILE__ << " at line " << __LINE__ << ": reference tree Newick format file is missing. Please specify it with the -r flag." << endl;
      printHelp(argv[0]);
      exit(1);
    }
  }

  // if ( inPar->treeFile ) // load ref tree
  // {
  //   if ( !nt.loadTree(inPar->treeFile) )
  //   {
  //     fprintf(stderr,"Could not load Newick tree from %s\n", inPar->treeFile);
  //     exit(EXIT_FAILURE);
  //   }
  // }
  // else
  // {
  //   fprintf(stderr, "ERROR in %s at line %d: Reference tree Newick format file is missing. Please specify it with the -r flag\n",  __FILE__, __LINE__);
  //   printHelp(argv[0]);
  //   exit(1);
  // }

  //fprintf(stderr, "After loading tree\n");

  int depth = nt.getDepth();
  cerr << "--- Depth of the tree: " << depth << endl;

  if ( inPar->outDir ) // create output directory
  {
    string cmd("mkdir -p ");
    cmd += string(inPar->outDir);
    system(cmd.c_str());
  }
  else
  {
    cout << endl << "ERROR in "<< __FILE__ << " at line " << __LINE__
	 << ": Output directory is missing. Please specify it with the -o flag." << endl;
    printHelp(argv[0]);
    exit(1);
  }

  int nModels = 0;

  if ( inPar->mcDir ) // extracting number of models and k-mer size
  {
    string inFile(inPar->mcDir);
    inFile += "/modelIds.txt";
    FILE *in = fopen(inFile.c_str(), "r");
    if ( !in )
    {
      cerr << "Cannot read model ids in " << __FILE__ << " at line " << __LINE__ << endl;
      exit(1);
    }
    fclose(in);

    vector<char *> modelIds;
    readLines(inFile.c_str(), modelIds);
    nModels = modelIds.size();

    for ( int i = 0; i < nModels; ++i )
      free(modelIds[i]);

    int k = 0;
    char countStr[5];
    sprintf(countStr,"%d",k);
    string file = string(inPar->mcDir) + string("/MC") + string(countStr) + string(".log10cProb");

    while ( exists( file.c_str() ) )
    {
      k++;
      sprintf(countStr,"%d",k);
      file = string(inPar->mcDir) + string("/MC") + string(countStr) + string(".log10cProb");
    }

    if ( (inPar->kMerLens.size() && inPar->kMerLens[0] > k) )
    {
      inPar->kMerLens[0] = k;
    }
    else if ( !inPar->kMerLens.size() )
    {
      //inPar->kMerLens.clear();
      inPar->kMerLens.push_back(k);
    }
  }
  else if ( !inPar->mcDir )
  {
    cout << endl << "ERROR in "<< __FILE__ << " at line " << __LINE__
	 << ": Please specify a directory with MC model files using -d flag." << endl;
    printHelp(argv[0]);
    exit(1);
  }


  if ( inPar->kMerLens.size() == 0 )
  {
    int kMers[] = {3};
    cerr << endl << "WARNING: Setting k-mer size to " << kMers[0] << endl;
    int n = sizeof(kMers) / sizeof(int);
    for ( int i = 0; i < n; ++i )
      inPar->kMerLens.push_back(kMers[i]);
  }

  cerr << "--- nModels=" << nModels << endl;

  int wordLen = inPar->kMerLens[0];

  if ( inPar->verbose )
    cerr << "\rk=" << wordLen << "\n";

  if ( inPar->mcDir && !inPar->trgFiles.size() )
    cerr << "\r--- Reading k-mer frequency tables from " << inPar->mcDir << " ... ";
  else
    cerr << "\r--- Generating k-mer frequency tables for k=1:" << wordLen << " ... ";

  size_t alloc = 1024*1024;
  char *data, *seq;
  MALLOC(data, char*, alloc * sizeof(char));
  MALLOC(seq, char*, alloc * sizeof(char));

  MarkovChains2_t *probModel;

  // loading MC models
  probModel = new MarkovChains2_t( wordLen-1,
				   inPar->trgFiles,
				   inPar->mcDir,
				   inPar->maxNumAmbCodes,
				   inPar->pseudoCountType );
  cerr << "done" << endl;

  //fprintf(stderr, "seq: %s\tseqLen=%d\n", seq, seqLen);

  vector<char *> modelIds = probModel->modelIds();
  vector<string> modelStrIds;
  probModel->modelIds( modelStrIds );
  nt.modelIdx( modelStrIds );

  // double *aLogOdds;
  // MALLOC(aLogOdds, double*, depth * sizeof(double));

  //int count = 0;
  // int depthCount;
  // string currentLabel;
  // double x[nModels]; // stores conditional probabilities p(x | M) for children of each node. the root node has 3 children

  // char str[10];
  // sprintf(str,"%d",(wordLen-1));
  // string outFile = string(inPar->outDir) + string("/") + string("MC.order") + string(str) + string(".results.txt");
  // FILE *out = fOpen(outFile.c_str(), "w");

  // traverse the reference tree using breath first search
  NewickNode_t *root = nt.root();
  NewickNode_t *node;
  NewickNode_t *sppnode;
  NewickNode_t *sibnode;
  NewickNode_t *pnode;
  queue<NewickNode_t *> bfs;
  bfs.push(root);
  int numChildren;
  int sampleSize = inPar->randSampleSize;

  fprintf(stderr, "Sample Size: %d\n", sampleSize);

  while ( !bfs.empty() )
  {
    node = bfs.front();
    bfs.pop();

    numChildren = node->children_m.size();

    if ( node != root )
    {
      fprintf(stderr, "\r--- Processing %s\n", node->label.c_str());

      string outFile = string(inPar->outDir) + string("/") + node->label + string(".txt");
      //fprintf(stderr, "\toutFile: %s\n\n", outFile.c_str());
      FILE *out = fOpen(outFile.c_str(), "w");

      // generate random sequences of 'node' model and write their log10 prob's to a file
      // debug
      // fprintf(stderr, "Generating sampleSize random sequences from model s->model_idx\n");

      vector<NewickNode_t *> leaves;
      nt.leafLabels( node, leaves);

      int nSpp = leaves.size();
      int nSeqsPerSpp = ceil( 1.0 * sampleSize / nSpp );
      cerr << "--- nSpp=" << nSpp << endl;
      cerr << "--- nSeqsPerSpp=" << nSeqsPerSpp << endl;
      //exit(1);

      char **seqTbl;

      for ( int k = 0; k < nSpp; ++k )
      {
	sppnode = leaves[k];
	//probModel->sample( &seqTbl, refSeqs, node->model_idx, sampleSize, seqLen ); // generate sampleSize random sequences from model s->model_idx
	probModel->sample( &seqTbl, sppnode->model_idx, nSeqsPerSpp, seqLen ); // generate sampleSize random sequences from model s->model_idx

	fprintf(out,"%s",node->label.c_str());
	for ( int s = 0; s < nSeqsPerSpp; ++s )
	  fprintf(out,"\t%f", probModel->normLog10prob(seqTbl[s], seqLen, sppnode->model_idx ));
	fprintf(out,"\n");

	for ( int j = 0; j < nSeqsPerSpp; ++j )
	  free(seqTbl[j]);
	free(seqTbl);
      }

      // identify siblings of node
      pnode = node->parent_m;
      vector<NewickNode_t *> siblings;
      int n = pnode->children_m.size();
      int nodeIdx = -1;
      for (int i = 0; i < n; i++)
	if ( pnode->children_m[i] != node )
	  siblings.push_back(pnode->children_m[i]);
	else
	  nodeIdx = i;

      #if 1
      //debug
      fprintf(stderr, "\tIdentifying siblings of %s\n", node->label.c_str());
      fprintf(stderr, "\tSiblings:\n");
      for (int i = 0; i < (int)siblings.size(); ++i )
	fprintf(stderr, "\t\t%s\n", siblings[i]->label.c_str());
      fprintf(stderr, "\n");
      #endif


      #if 0
      string faFile = string(inPar->faDir) + string("/") + node->label + string(".fa");
      map<string, string> refSeqs;
      readFasta( faFile.c_str(), refSeqs);

      fprintf(stderr,"faFile: %s\n",faFile.c_str());

      if ( node->label == "f_Lachnospiraceae" )
      {
	int nref = refSeqs.size();
	map<string, string>::iterator itr;
	fprintf(stderr,"%s\t",node->label.c_str());
	for ( itr = refSeqs.begin(); itr != refSeqs.end(); ++itr )
	  fprintf(stderr,",%f", probModel->normLog10prob(itr->second.c_str(), (int)itr->second.size(), node->model_idx ));

	for ( int s = nref; s < sampleSize; ++s )
	  fprintf(stderr,",0");
	fprintf(stderr,"\n");
      }
      #endif

      n = siblings.size();

      // fprintf(stderr, "num siblings: %d\n", n);

      //vector<double> w(n); // proportion of sampleSize for which p(x|M) = max_{j=0...n} p(x|M_j) where M is the model associated with node and x is a random sequence of M_i.
      for (int i = 0; i < n; i++) // for each sibling s
      {
	sibnode = siblings[i];

	// fprintf(stderr, "\ri: %d  sibnode->label: %s\n", i, sibnode->label.c_str());

        #if 0
	faFile = string(inPar->faDir) + string("/") + sibnode->label + string(".fa");
	refSeqs.clear();
	readFasta( faFile.c_str(), refSeqs);

	for ( itr = refSeqs.begin(); itr != refSeqs.end(); ++itr )
	  fprintf(out,"\t%f", probModel->normLog10prob(itr->second.c_str(), (int)itr->second.size(), node->model_idx ));

	for ( int s = nref; s < sampleSize; ++s )
	  fprintf(out,"\t0");
	fprintf(out,"\n");
	#endif

	//probModel->sample( &seqTbl, refSeqs, sibnode->model_idx, sampleSize, seqLen ); // generate sampleSize random sequences from model s->model_idxw
	probModel->sample( &seqTbl, sibnode->model_idx, sampleSize, seqLen ); // generate sampleSize random sequences from model s->model_idx

	#if 0
	int iw = 0; // number of times   p(x|M) = max_{j=0...n} p(x|M_j) where M is the model associated with node and x is a random sequence of M_i.
	for ( int s = 0; s < sampleSize; ++s )
	{
	  int m = pnode->children_m.size();
	  double x[m];
	  for ( int j = 0; j < m; j++ )
	    x[j] = probModel->normLog10prob(seqTbl[s], seqLen, (pnode->children_m[j])->model_idx );

	  if ( nodeIdx == which_max( x, m ) )
	    iw++;
	}
	//w[i] = (double)iw / sampleSize;
	#endif

	// write to file log10 prob's of random sequences of s model
	fprintf(out,"%s",sibnode->label.c_str());
	for ( int s = 0; s < sampleSize; ++s )
	  fprintf(out,"\t%f", probModel->normLog10prob(seqTbl[s], seqLen, node->model_idx ));
	fprintf(out,"\n");

	for ( int j = 0; j < sampleSize; ++j )
	  free(seqTbl[j]);
	free(seqTbl);
      }

      fclose(out);
    }

    if ( numChildren )
    {
      for (int i = 0; i < numChildren; i++)
      {
	bfs.push(node->children_m[i]);
      }
    }
  }

  fprintf(stderr,"\r\nOutput written to %s\n", inPar->outDir);

  delete probModel;

  return EXIT_SUCCESS;
}



//----------------------------------------------------------- parseArgs ----
//! parse command line arguments
void parseArgs( int argc, char ** argv, inPar2_t *p )
{
  int c, errflg = 0;
  optarg = NULL;

  static struct option longOptions[] = {
    {"print-counts"       ,no_argument, &p->printCounts,    1},
    {"max-num-amb-codes"  ,required_argument, 0,          'b'},
    {"fasta-dir"          ,required_argument, 0,          'f'},
    {"out-dir"            ,required_argument, 0,          'o'},
    {"ref-tree"           ,required_argument, 0,          'r'},
    {"pseudo-count-type"  ,required_argument, 0,          'p'},
    {"sample-size"        ,required_argument, 0,          's'},
    {"help"               ,no_argument, 0,                  0},
    {0, 0, 0, 0}
  };

  while ((c = getopt_long(argc, argv,"b:d:e:f:t:i:k:o:vp:r:s:h",longOptions, NULL)) != -1)
    switch (c)
    {
      case 'b':
	p->maxNumAmbCodes = atoi(optarg);
	break;

      case 's':
	p->randSampleSize = atoi(optarg);
	break;

      case 'r':
	p->treeFile = strdup(optarg);
	break;

      case 'e':
	p->seqID = strdup(optarg);
	break;

      case 'p':
	{
	  int pc = atoi(optarg);
	  if ( pc == -1 )
	  {
	    p->pseudoCountType = zeroOffset0;
	  }
	  else if ( pc == 0 )
	  {
	    p->pseudoCountType = zeroOffset1;
	  }
	  else if ( pc == 1 )
	  {
	    p->pseudoCountType = zeroOffset1;
	  }
	  else if ( pc == 2 )
	  {
	    p->pseudoCountType = recPdoCount;
	  }
	  else
	  {
	    cerr << "ERROR in " << __FILE__ << " at line " << __LINE__ << ": Undefined pseudo-count type" << endl;
	    exit(1);
	  }
	}
	break;

      case 'd':
	p->mcDir = strdup(optarg);
	break;

      case 'f':
	p->faDir = strdup(optarg);
	break;

      case 'o':
	p->outDir = strdup(optarg);
	break;

      case 't':
	p->trgFile = strdup(optarg);
	break;

      case 'i':
	p->inFile = strdup(optarg);
	break;

      case 'k':
	parseCommaList(optarg, p->kMerLens);
	break;

      case 'v':
	p->verbose = true;
	break;


      case 'h':
	printHelp(argv[0]);
	exit (EXIT_SUCCESS);
	break;

      case 0:
	break;

      default:
	cerr << "\n"
	     << "=========================================\n"
	     << " ERROR: Unrecognized option " << (char)c << "\n" << endl;

	for ( int i=0; i < argc; ++i )
	  cerr << argv[i] << " ";
	cerr << endl;

	cerr << "==========================================\n" << endl;
	++errflg;
	break;
    }

  if ( errflg )
  {
    printUsage(argv[0]);
    cerr << "Try '" << argv[0] << " -h' for more information" << endl;
    exit (EXIT_FAILURE);
  }

  for ( ; optind < argc; ++ optind )
    p->trgFiles.push_back( strdup(argv[optind]) );
}
