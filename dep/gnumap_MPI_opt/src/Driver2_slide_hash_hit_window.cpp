/*
this is a temp main program to make gnumap non-directional mapping!
*/

#ifdef	MPI_RUN
#include <mpi.h>
#endif

#ifdef OMP_RUN
#include <omp.h>
#endif

#include <signal.h>
#include <vector>
#include <ctime>
#include <sys/time.h>
#include <sys/resource.h>
#include <cmath>
#include <set>
#include <map>
#include <pthread.h>
#include <unistd.h>

#include <algorithm>
#include <exception>
#include "const_include.h"
#include "const_define.h"	// Defines many things in this file
//#include "GenomeMem.h"
#ifdef GENOME_STL
#include "GenomeSTL.h"
#else
#include "GenomeMem.h"
#endif
#include "bin_seq.h"
#include "SeqManager.h"
#include "ScoredSeq.h"
#include "BSScoredSeq.h"
#include "NormalScoredSeq.h"
#include "SNPScoredSeq.h"
//#include "HMMScoredSeq.h"
#include "SequenceOperations.h"
#include "centers.h"
#include "Exception.h"

#ifdef DEBUG_NW
unsigned int num_nw[8] = {0,0,0,0,0,0,0,0};
#endif

typedef set<ScoredSeq*,ScoredSeq > seq_set;
typedef map<string,ScoredSeq*> seq_map;
//typedef map<string,ScoredSeq*> seq_premap;

const char* seq_file = 0;
const char* genome_file = 0;
const char* output_file = "gnumap_out";
ofstream of;
const char* pos_matrix = NULL;
bool gPRINT_ALL_SAM = false;

/*****************************************************
 * These next functions are used in parsing command-
 * line arguments
 *****************************************************/
int ParseCmdLine(const int argc, const char* argv[]);
void GetParseError(ostream &os, const char* argv[]);
int set_arg_ext(const char* param, const char* assign, int &count);
int set_arg(const char* param, const char* assign, int &count);
CMDLine this_cmd;
/*****************************************************/
unsigned int align_sequence(Genome &genStrand, seq_map &unique, const Read &search, const string &consensus, double min_align_score, double near_max_align_score, double &denominator, double &top_align_score, int &cnt_summit, int strand2ali, int align_mode, int thread_id);

/*****************************************************
 * These next few are all for multi-threading
 *****************************************************/
unsigned int gNUM_THREADS = 1;
volatile bool setup_complete = false;	// global signal for complete setup
volatile bool finished = false;			// global signal for finishing everything
unsigned int *finished_arr = 0;					// Array used to tell when threads are done

double gTimeBegin;

pthread_attr_t attr;
pthread_mutex_t lock;
pthread_mutex_t nwlock;
pthread_mutex_t total_read_lock;
pthread_mutex_t cond_lock;
#ifdef DEBUG_TIME
double cond_lock_time = 0;
double read_lock_time = 0;
double write_lock_time = 0;
#endif

pthread_mutex_t comm_barrier_lock;
pthread_cond_t comm_barrier_cond = PTHREAD_COND_INITIALIZER;
volatile unsigned int cond_count = 0;
volatile unsigned int cond_thread_num;
//pthread_mutex_t read_lock;

// The writer thread
pthread_t writer;
pthread_mutex_t write_lock;

void* parallel_thread_run(void* t_opts);
struct thread_rets* omp_thread_run(struct thread_opts*);
void* mpi_thread_run(void* t_opts);
void getMoreReads(unsigned int &begin, unsigned int &end);
volatile unsigned int gReadsDone = 0;
volatile unsigned int iter_num = 0;
unsigned int nReadPos;
unsigned int nReadPerProc;

/*
const unsigned int nReads = 1024*8;
Read* gReadArray[nReads];
seq_map gReadLocs[nReads];
double gReadDenominator[nReads];
double gTopReadScore[nReads];
*/
unsigned int nReads;
Read** gReadArray;	// allocate dynamically
seq_map* gReadLocs;	// allocate dynamically

double* gReadDenominator;
double* gTopReadScore;

vector<TopReadOutput> gTopReadOutput;

#ifdef MPI_RUN
MPI::Status status;
#ifdef DISCRETIZE
/* center_e is relly a character (so it holds 128 bits) */
void MPISumCenters( const void *in, void *inout, int len, const MPI::Datatype &dptr ) {
	int i;
	center_d *a = (center_d*)in;
	center_d *b = (center_d*)inout;

//void MPISumCenters( const center_d *in, center_d *intou, int len, const MPI::Datatype &dptr ) {
//	int i;
	center_d c;

	for(i=0; i< len; i++) {
		// The sum has been pre-calculated and is just a lookup in this table
		c = sum_mat[*a][*b];
		*b = c;
		a++; b++;
	}
}
#endif // DISCRETIZE
#endif // MPI_RUN

#ifdef GENOME_STL
typedef GenomeSTL GENOME_t;
#else
typedef GenomeMem GENOME_t;
#endif

GENOME_t gGen;
//gGen = new GENOME_t[2];

SeqManager* gSM;

void usage(int has_error, char* errmessage) {
	if(has_error) cout << endl << errmessage << endl ;

	cout << "Usage: gnumap [options] <file_to_parse>\n"
		 << "  -g, --genome=STRING          Genome .fa file(s)\n"
		 << "  -o, --output=STRING          Output file\n"
		 << "  -v, --verbose=INT            Verbose (default=0)\n"
		 << "  -c, --num_proc=INT           Number of processors on which to run\n"
		 << "  -B, --buffer=INT             Buffer size\n"
		 << "  -?, --help                   Show this help message\n"
		 << "\n"
		 << "Options for Alignment\n"
		 << "  -a, --align_score=DOUBLE     Limit for sequence alignment (default: 90%)\n"
		 << "  -r, --raw                    Use raw score when determining alignment cutoff\n"
		 << "  -G, --gap_penalty=DOUBLE     Gap Penalty for Substitution Matrix\n"
		 << "  -A, --adaptor=STRING         Adaptor sequence to remove from sequences\n"
		 << "  -M, --max_gap=INT            Maximum Number of Gaps to use in Alignment\n"
		 << "  -S, --subst_file=STRING      Position-Weight Matrix file for DNA\n"
		 << "                               Substitutions\n"
		 << "  -T, --max_match=INT          Maximum number of matches to print out for a given\n"
		 << "                               sequence (default: "<<gMAX_MATCHES<<")\n"
		 << "  -t, --max_match2=INT         Same as the option with -T but it won't print anything\n"
		 << "                               if max num of matches is larger than it\n"		 
		 << "  -u, --unique_only            Only match sequences that map uniquely\n"
		 << "                               (default: not included)\n"
		 /*
		 << "  -j, --jump=INT               The number of bases to jump in the sequence hashing\n"
		 << "                               (default: mer_size)\n"
		 << "  -k, --top_seed=INT       If you set k to 2, any hits larger than maxhit-1 will be\n"
		 << "                               considered for alignment (default 2)\n"
		 */
		 << "  --up_strand                  Will only search the positive strand of the genome\n"
		 << "                               for matching location (will not look for reverse\n"
		 << "                               compliment match to the genome.\n"
		 << "  --down_strand                Will only search the negaitve strand (opposite of\n"
		 << "                               --up_strand command)\n"
		 << "\n"
		 << "Options for Read Quality\n"
		 << "  -q, --read_quality=DOUBLE    Read quality cutoff:  won't align reads if they are\n"
		 << "                               below this cutoff value\n"
		 << "                               (default=0.0)\n"
		 << "  --illumina                   Offset PHRED score w/ 64\n"
		 << "  --top_hash                   Consider a set of position with the max number of hash hits only (faster)\n"
		 << "  --noqual                     base quality won't be applied\n"
		 << "\n"
		 << "Options for Creating Hash and Genome\n"
		 << "  -m, --mer_size=INT           Mer size (default="<<DEF_MER_SIZE<<")\n"
		 << "  -s, --gen_skip=INT           Number of bases to skip when the genome is aligned\n"
		 << "  -h, --max_hash=INT           Maximum values to store in the hash.\n"
		 << "                               (default: 1000 )\n"
		 << "  --bin_size=INT               The resolution for GNUMAP (default: 8)\n"
		 << "\n"
		 << "Options for Printing:\n"
		 << "  -0, --print_full             Print locations for the entire sequence, not\n"
		 << "                               just for the beginning.\n"
		 << "  --print_all_sam              Include all possible SAM records in output\n"
		 << "  --vcf                        Prints VCF format instead of gmp (default: false)\n"
		 << "\n"
		 << "Options for extra GNUMAP runs\n"
		 << "  --snp                        Turn on SNP mapping (will output a .sgrex file)\n"
		 << "  --snp_pval=DOUBLE            P-Value cutoff for calling SNPs\n"
		 << "                               (default: "<<gSNP_PVAL<<")\n"
		 << "  --snp_monop                  Flag that turns on monoploid SNP calling\n"
		 << "                               (default: diploid SNP calling)\n"
		 << "  -b, --bs_seq                 Flag to turn on the C to T conversion, used in\n"
		 << "                               bisulfite sequence analysis\n"
		 << "  -d, --a_to_g                 Flag that allows for A to G conversion\n"
		 << "  --fast                       Perform a fast alignment (at some reduction\n"
		 << "                               of accuracy)\n"
		 << "                               (default: none)\n"
		 << "  --gmp                        generating sgr/gmp file, a pileup process (default: no)\n"
		 << "  --sw                         perform soft clipped mapping (currently in ALPHA)\n"
		 << "  -n, --ref_size=INT            the length of the original reference to be aligned in bp(default: the length of input reference)\n"
		 << "  --assembler                  Turns on flags for assembler output (currently in ALPHA)\n"
		 << "\n"
		 << "For MPI usage:\n"
		 << "  --MPI_largemem               If the run requires a large amount of memory, this\n"
		 << "                               flag will spread it accross several nodes.\n"
		 << "                               (default:  not included)\n"
		 << "\n"
		 << "For reading and writing to a binary file:\n"
		 << "  --save=FILENAME              Save the genome out to a file\n"
		 << "  --read=FILENAME              Read the genome in from a file\n"
		 << "\n";
	exit(1);
}

/*
struct GenomeLocation {
	GenomeLocation() {
		amount = 0.0;
		packed_char = 0;
	}

	GenomeLocation(GEN_TYPE pc, float amt)
		: amount(amt), packed_char(pc) {}

	float amount;
	GEN_TYPE packed_char;
};
*/

/************************************
 * MPI Structures and DataTypes 	*
 ************************************/
 #ifdef MPI_RUN
 /**
  * This is an MPI function for performing a reduce with user-defined data types.
  *
  * @param in One of the vectors of length /len/
  * @param inout The other vector to sum, which will contain the final result as well.
  * 		Also length /len/.
  * @param len The length of /in/ and /inout/
  * @param dptr The MPI Datatype of this function (not used?)
  */
 void sumGen( GenomeLocation *in, GenomeLocation *inout, int *len, MPI_Datatype *dptr ) {
 	int i;
 	
 	for(i=0; i<*len; i++) {
 		inout->amount = in->amount+inout->amount;
		
 		++in; ++inout;
 	}
 }
 
 #endif

/**
 * We have a loss of precision with some log additions. So we'll just add in the log space
 * and go from there.
 */
/*
inline float LogSum(float a, float b) {
	if(a > b && (a != 0)
		return a + log(1+exp(b-a));
	else
		return b + log(1 + exp(a-b));
}
*/
/**
 * This is the same as the above function, only it requires the input to be the powers
 * of e.  This way, we don't need to take the log of anything.
 */
/*inline float ExpLogSum(float expA, float expB) {
	
}
*/

/**
 * Because this is used to produce the consensus sequence, we need ambiguity characters...
 */
//inline char max_char(vector<double> &chr) {
inline char max_char(float* chr) {
	if((chr[0] == chr[1]) && (chr[0] == chr[2]) && (chr[0] == chr[3]))
		return 'n';
	if(chr[0] >= chr[1]) {
		if(chr[0] >= chr[2]) {
			if(chr[0] >= chr[3])
				return 'a';
			else
				return 't';
			}
		else {
			if(chr[2] >= chr[3])
				return 'g';
			else
				return 't';
		}
	}
	else {
		if(chr[1] >= chr[2]) {
			if(chr[1] >= chr[3])
				return 'c';
			else
				return 't';
		}
		else {
			if(chr[2] >= chr[3])
				return 'g';
			else return 't';
		}
	}

	return 'n';
}
 
//inline string GetConsensus(vector<vector<double> > &read) {
inline string GetConsensus(Read &read) {
	if(read.seq.size() > 0) {
		return read.seq;
	}

	string seq = "";

	for(unsigned int i=0; i<read.length; i++) {
		seq += max_char(read.pwm[i]);
	}

	return seq;
}

//I don't think this function is even used...
//vector<vector<double> > subvector(vector<vector<double> > &vec, int start, int length) {
float** subvector(float** &vec, int start, int length) {
	//vector<vector<double> > to_return;
	float** to_return = new float*[length];

	for(int i=start,j=0; i<start+length; i++,j++) {
		to_return[j] = new float[4];
		for(int k=0; k<4; k++) {
			to_return[j][k] = vec[i][k];
		}
		delete[] vec[i];
	}
	
	delete[] vec;	//we want to free the memory for vec because we're creating a new
					//one, right??
	
	return to_return;
}

struct thread_opts {
	thread_opts(GENOME_t &g, SeqManager &s, int ti) :
		gen(g), sm(s), thread_id(ti) {}
	
	GENOME_t &gen;
	SeqManager &sm;
	//vector<vector<double> >* seqs;
	unsigned int num_seqs;
	int thread_id;
};

struct thread_rets {
	ostringstream* of;
	unsigned int good_seqs;
	unsigned int bad_seqs;
};

void clearMapAt(unsigned int index) {

	seq_map::iterator sit;
	//seq_premap::iterator sitp;
		
	if (!(gReadLocs[index].empty())) {
		for(sit = gReadLocs[index].begin(); sit != gReadLocs[index].end(); ++sit) { 
			if((*sit).second)
				delete (*sit).second;
		}
		gReadLocs[index].clear();
	}
	
}


/*
#ifdef SET_POS
void clearPositions(GENOME_t &gen, vector<unsigned long> &positions, int thread_id) {
	for(unsigned int i=0; i<positions.size(); i++) {
		gen.unsetThreadID(positions[i],thread_id);
	}
}
#endif
*/

//#include "align_seq3_raw.cpp" //replaced by CJ

void set_top_matches(GENOME_t &gen, unsigned int rIndex, string &consensus,
			unsigned int &num_not_matched, unsigned int &num_matched, unsigned int &thread_id) {

	bin_seq bs;
	double min_align_score = 0;
	double near_max_align_score = 0;
	double top_align_score_pos = 0;
	double top_align_score_neg = 0;
	double denominator = 0;
	unsigned int cnt_map = 0;
	int cnt_summit = 0;
	
	bool poorRead = true;
	
	//float sw_len_ratio;
	Read* search = gReadArray[rIndex];
	seq_map* unique = &gReadLocs[rIndex];

	
	if (search->length < gMinReadL) {
		num_not_matched++;
		gReadDenominator[rIndex] = 0;
		gTopReadScore[rIndex] = READ_TOO_SHORT;
		clearMapAt(rIndex);
		return;
	}

	// Too short to align
	if(search->length < (unsigned int)gMER_SIZE) { 
#ifdef DEBUG
		fprintf(stderr,"%s Too short to align\n",consensus.c_str());
#endif
		num_not_matched++;
		gReadDenominator[rIndex] = 0;
		gTopReadScore[rIndex] = READ_TOO_SHORT;
		clearMapAt(rIndex);
		return;
	}
	
	// We need to do the length-1 because it will go to the base at that position.
	double max_align_score = 0.0; 
	
/*	
#ifdef DEBUG

//	if(gVERBOSE > 2) {
		cerr << "\nNext Read ("<< search->name << ") aligned with " << max_align_score << ", less than " << gCUTOFF_SCORE << "?\n";
		cerr << consensus << "\n";
//	}
#endif
*/
	max_align_score = bs.get_align_score(*search,consensus,0,search->length-1);
	
	// Too low of quality to align
	if(max_align_score < gCUTOFF_SCORE) {
#ifdef DEBUG
		fprintf(stderr,"%s:%s didn't meet cutoff: %f vs %f\n",consensus.c_str(),search->fq.c_str(),max_align_score,gCUTOFF_SCORE);
#endif
		num_not_matched++;
		gReadDenominator[rIndex] = 0;
		gTopReadScore[rIndex] = READ_TOO_POOR;
		clearMapAt(rIndex);
		return;
	}
	
	if(perc)
		min_align_score = gALIGN_SCORE * max_align_score;
	else
		min_align_score = gALIGN_SCORE;
	
	near_max_align_score = max_align_score - SAME_DIFF;
	if (gMATCH_POS_STRAND) {

		//fprintf(stderr,"Aligning to UP_Strand\n");
		// append both ends to the original read to get an accurate align transcription
		poorRead = false;
		cnt_map = align_sequence(gen, *unique, *search, consensus, 
									min_align_score, near_max_align_score, denominator, top_align_score_pos, cnt_summit, POS_STRAND, NW_ALIGN, thread_id);
	}
	
	if (gUNIQUE && (cnt_summit>1)) {
			clearMapAt(rIndex);

			num_matched++;
			gReadDenominator[rIndex] = 0;
			gTopReadScore[rIndex] = READ_TOO_MANY;
			return;
	}
	
	if (gMATCH_NEG_STRAND) {
		string rc_consensus = reverse_comp(consensus);	
			// Copy it so we don't need to reverse it again
		float** rc_pwm = reverse_comp_cpy(search->pwm,search->length); //integrate end extension
		Read rc(rc_pwm,search->length);
		rc.name = search->name;

		//fprintf(stderr,"Aligning to UP_Strand\n");
		// append both ends to the original read to get an accurate align transcription
		poorRead=false;
		cnt_map += align_sequence(gen, *unique, rc, rc_consensus, 
								min_align_score, near_max_align_score, denominator, top_align_score_neg, cnt_summit, NEG_STRAND, NW_ALIGN, thread_id);

			// Clean up the copy
		for(unsigned int i=0; i<rc.length; i++) {
			delete[] rc_pwm[i];
		}
		delete[] rc_pwm;
	}
	
		// Too low of quality to align
	if(poorRead) {
#ifdef DEBUG
		fprintf(stderr,"%s:%s didn't meet cutoff: %f vs %f\n",consensus.c_str(),search->fq.c_str(),
							max_align_score,gCUTOFF_SCORE);
#endif
		num_not_matched++;
		gReadDenominator[rIndex] = 0;
		gTopReadScore[rIndex] = READ_TOO_POOR;
		clearMapAt(rIndex);
		return;
	}
	
	if (gUNIQUE && (cnt_summit>1)) {
// 	if ((gSKIP_TOO_MANY_REP && (cnt_map > gMAX_PRE_MATCHES)) 
// 		|| (gUNIQUE && (cnt_summit>1))) {
#ifdef DEBUG
			fprintf(stderr,"no mapping: reason: %d\n",aligned);
#endif
			clearMapAt(rIndex);

			num_matched++;
			gReadDenominator[rIndex] = 0;
			gTopReadScore[rIndex] = READ_TOO_MANY;
			return;
	}

#ifdef DEBUG
	fprintf(stderr,"**[%d/%d] Sequence [%s] has %lu matching locations\n",iproc,nproc,consensus.c_str(),unique->size());
#endif
	
	if(gReadLocs[rIndex].size() == 0) { //NOTHING2MAP?
		num_not_matched++;
		gReadDenominator[rIndex] = 0;
		gTopReadScore[rIndex] = 0;
		return;
	}
		
	//fprintf(stderr,"Index %d has %u elements\n",rIndex,gReadLocs[rIndex].size());
	num_matched++;
	gReadDenominator[rIndex] = denominator;
#ifdef DEBUG
	fprintf(stderr,"Index %d has denominator of %f\n",rIndex,denominator);
#endif
	
	gTopReadScore[rIndex] = top_align_score_pos;
	if (top_align_score_pos < top_align_score_neg)
		gTopReadScore[rIndex] = top_align_score_neg;

	return;
}

void create_match_output(GENOME_t &gen, unsigned int rIndex, string &consensus) {

#ifdef DEBUG
	fprintf(stderr,"[%d/%d] [%s] has %lu matches\n",iproc,nproc,gReadArray[rIndex]->name,gReadLocs[rIndex].size());
#endif

	if(gReadLocs[rIndex].size() == 0) {
#ifdef DEBUG
		if(gTopReadScore[rIndex] != READ_TOO_POOR)
			fprintf(stderr,"[%d/%d] Sequence [%s] does not have any matches\n", iproc, nproc, gReadArray[rIndex]->name);
#endif
		return;
	}
	
	//to_return << endl;
	//to_return << "Denominator: " << denominator << endl;
	seq_map::iterator sit;
	//set<pair<unsigned long, int> >::iterator sitp;
	
	double denom_new = 0.0;
	double myAlignScore, nearOptimal;
	double bestAlignScore=0.0;
	
	//gReadDenominator[rIndex]=0.0;
	//unsigned int rL = gPRINT_FULL ? consensus.length() : 1;
	unsigned int rL = consensus.length();
	unsigned i, rLx;
	int dummy = -1;
	map<unsigned long, unsigned int> refMapLoc;

	Read* read = gReadArray[rIndex];
	vector<TopReadOutput> out_vec;
	
	if (gMATCH_NEG_STRAND)
		rLx = rL;
	else
		rLx = 1;
	
	float** rev_pwm = reverse_comp_cpy(read->pwm,rLx);
	Read rc(rev_pwm,rLx);
	rc.name = read->name;
	string rc_consensus = reverse_comp(consensus);
	
	if (read->fq.size()>0)
		rc.fq = reverse_qual(read->fq,rLx);

	for(sit = gReadLocs[rIndex].begin(); sit != gReadLocs[rIndex].end(); ++sit) {
		//this routine perform more accurate alignment and resolves all multiple tops

		if ((*sit).second->get_strand()==POS_STRAND)
			(*sit).second->score(gReadDenominator[rIndex],gGen,rL,*read,refMapLoc,denom_new,lock);
		else
			(*sit).second->score(gReadDenominator[rIndex],gGen,rL,rc,refMapLoc,denom_new,lock);

		// Here's the flag for printing a SAM record at every position
		if(gPRINT_ALL_SAM) {
// vector<TopReadOutput> out_vec = (*sit).second->get_SAM(gReadDenominator[rIndex],*gReadArray[rIndex],consensus,gGen,rIndex);
			(*sit).second->get_SAM2(gReadDenominator[rIndex],*read,consensus,gGen,rIndex,dummy,refMapLoc,out_vec);
#ifdef DEBUG
			fprintf(stderr,"[%d/%d] \tFor sequence, %ld positions:\n",iproc,nproc,out_vec.size());
#endif
			MUTEX_LOCK(&write_lock);
			vector<TopReadOutput>::iterator vit = out_vec.begin();
			for(; vit != out_vec.end(); vit++) {
#ifdef DEBUG
				fprintf(stderr,"[%d/%d] Pushing back %s (%f)...size: %lu\n",iproc,nproc,vit->CHR_NAME,vit->POST_PROB, gTopReadOutput.size());
#endif
				gTopReadOutput.push_back(*vit);
			}
			MUTEX_UNLOCK(&write_lock);
		}

		myAlignScore=(*sit).second->get_score();
		if (myAlignScore>bestAlignScore)
			bestAlignScore=myAlignScore;
	}
	
	//fprintf(stderr,"MAX IS %f and size is %d and DENOM is %f\n",max->get_score(), gReadLocs[rIndex].size(), gReadDenominator[rIndex]);
	
	//update stats
	gReadDenominator[rIndex] = denom_new;
	gTopReadScore[rIndex] = bestAlignScore;
	
	// We've already printed the max, so we'll go ahead and quit now
	if(gPRINT_ALL_SAM) {
		//delete max;
		return;
	}

	//now, we know what is the top score and then let us print top alignments
	nearOptimal = bestAlignScore - SAME_DIFF;

	bool skipMap = false;
	int multipleTopsCnt;
	int multipleTopsCntPos = 0;
	int multipleTopsCntNeg = 0;
	int total_max_match_allowed = (int)gMAX_MATCHES;
	
	if (gMATCH_POS_STRAND && gMATCH_NEG_STRAND)
		total_max_match_allowed = 2*gMAX_MATCHES;
	
	if(	gTopReadScore[rIndex] != READ_TOO_SHORT &&
			gTopReadScore[rIndex] != READ_TOO_POOR &&
			gTopReadScore[rIndex] != READ_TOO_MANY ) {
		
		//print near-top (or multiple best) in sam format
		vector<TopReadOutput> out_vec;
		for(sit = gReadLocs[rIndex].begin(); sit != gReadLocs[rIndex].end(); ++sit) {
			
			if ((*sit).second->get_score() >= nearOptimal) {
				multipleTopsCnt = multipleTopsCntPos+ multipleTopsCntNeg;
				if (multipleTopsCnt > total_max_match_allowed)  {
					if (gSKIP_TOO_MANY_REP) {skipMap = true;}
					break;
				}
				
				if (gUNIQUE && (multipleTopsCnt > 1)) {
					skipMap = true;
					break;
				}
				
				if ((*sit).second->get_strand() == POS_STRAND)
					(*sit).second->get_SAM2(gReadDenominator[rIndex],*read,consensus,gen,rIndex,multipleTopsCntPos,refMapLoc,out_vec);
				else
					(*sit).second->get_SAM2(gReadDenominator[rIndex],rc,rc_consensus,gen,rIndex,multipleTopsCntNeg,refMapLoc,out_vec);

			} //end of if
			
		} //end of for
		
		if (!skipMap) {
			MUTEX_LOCK(&write_lock);
			vector<TopReadOutput>::iterator vit = out_vec.begin();
			for(; vit != out_vec.end(); vit++)
				gTopReadOutput.push_back(*vit);

			MUTEX_UNLOCK(&write_lock);
		}
		//okay, we are done with this index and cleanup map_loc hash
		clearMapAt(rIndex);
	}
	// We want to add this to the vector if this isn't the largemem option.  If it is, we'll
	// only add if this is the writer node
	else if(!gMPI_LARGEMEM || (iproc == 0) ) {
		TopReadOutput tro;
		tro.consensus = consensus;
		tro.qual = str2qual(*gReadArray[rIndex]);
		strncpy(tro.READ_NAME,gReadArray[rIndex]->name,MAX_NAME_SZ);
		//tro.MAPQ = gTopReadScore[rIndex];	// Flag for a bad read
		// Do we need to have the specific failure process?
		tro.MAPQ = READ_FAILED;

		//fprintf(stderr,"Setting it to READ_FAILED here\n");

#ifdef DEBUG_TIME	
		double wait_begin = When();
#endif
		MUTEX_LOCK(&write_lock);
#ifdef DEBUG_TIME
		double wait_end = When();
		write_lock_time += wait_end - wait_begin;
#endif
		gTopReadOutput.push_back(tro);
		MUTEX_UNLOCK(&write_lock);
	}

	// Clean up the copy
	for(i=0; i<rLx; i++) {
		delete[] rev_pwm[i];
	}
	delete[] rev_pwm;

	return;
}

/**
 * This function will print the corresponding strings for all locations at this hash
 */
string print(GENOME_t &gen, HashLocation* pos, long begin, int size) {
	string to_return = "";
	
	//cout << "matches: " << pos.size() << endl;
	for(unsigned long i=0; i<pos->size; i++) {
		to_return += gen.GetString(pos->hash_arr[i] - begin, size) + "\t";
	}
	
	return to_return;
}


/* GetPWM is used for getting a PWM matrix representing the match (m) and mismatch (mm)
 * penalties.
 * The input is a file, which should be a 4x4 matrix of the format:
 * 				 A   C   G   T
 * 			A    m  mm  mm  mm
 *			C   mm   m  mm  mm
 * 			G   mm  mm   m  mm
 * 			T   mm  mm  mm   m
 * For ease in reading, the letters defining the rows and columns are disregarded.
 *
 *
 * Note:  GetPWM will modify the global gALIGN_SCORES variable.
 */
void readPWM(const char* fn) {
	gADJUST = 1;	// don't adjust the scores

	float temp_ALIGN_SCORES[5][4];

	int THIS_BUFFER = 100;
	
	ifstream in;
	in.open(fn);
	char temp_chr[THIS_BUFFER];
	bool contains_labels=false;
	float a,c,g,t;
	char label[THIS_BUFFER];
	char alabel[THIS_BUFFER];
	char clabel[THIS_BUFFER];
	int count=0;
	int num_lines=5;

	// Get all the lines of input
	while(in.getline(temp_chr,THIS_BUFFER) && count < num_lines) {
		if(sscanf(temp_chr,"%s %s %s %s",alabel,clabel,label,label) == 4 
				&& (tolower(*alabel) == 'a')
				&& (tolower(*clabel) == 'c') ) {
			if(gVERBOSE > 1)
				printf("Matched here: %s\n",temp_chr);
			in.getline(temp_chr,THIS_BUFFER);
			contains_labels=true;
		}
		if(gVERBOSE > 1)
			cout << "Line: " << temp_chr << endl;

		if(contains_labels) {
			if(sscanf(temp_chr,"%s %f %f %f %f",label,&a,&c,&g,&t) != 5) {
				char* error = new char[THIS_BUFFER];
				strcat(error,"Error in Score File: ");
				strcat(error,temp_chr);
				throw(error);
			}
		}
		else {
			if(sscanf(temp_chr,"%f %f %f %f",&a,&c,&g,&t) != 4) {
				char* error = new char[THIS_BUFFER];
				strcat(error,"Error in Score File: ");
				strcat(error,temp_chr);
				throw(error);
			}
		}
		
		temp_ALIGN_SCORES[count][0] = (float)a;
		temp_ALIGN_SCORES[count][1] = (float)c;
		temp_ALIGN_SCORES[count][2] = (float)g;
		temp_ALIGN_SCORES[count][3] = (float)t;

		count++;
	}
	if(count < num_lines) {
		throw new Exception("Error in Score File:  Not enough lines");
	}

	cout << "Count: " << count << "\tNum lines: " << num_lines << endl;

#ifdef DEBUG
	//for testing purposes...
	if(gVERBOSE > 1) {
		cout << "Matrix: " << endl;
		cout << "\t0\t1\t2\t3" << endl;
		for(int i=0; i<5; i++) {
			cout << i << "\t";
			for(int j=0; j<4; j++) 
				cout << temp_ALIGN_SCORES[i][j] << "\t";
			cout << endl;
		}
		cout << endl;
		//throw new Exception("Passed.");
	}
#endif

	for(unsigned int i=0; i<4; i++) {
		gALIGN_SCORES[(unsigned int)'a'][i] = temp_ALIGN_SCORES[0][i];
		gALIGN_SCORES[(unsigned int)'c'][i] = temp_ALIGN_SCORES[1][i];
		gALIGN_SCORES[(unsigned int)'g'][i] = temp_ALIGN_SCORES[2][i];
		gALIGN_SCORES[(unsigned int)'t'][i] = temp_ALIGN_SCORES[3][i];
		gALIGN_SCORES[(unsigned int)'n'][i] = temp_ALIGN_SCORES[4][i];
	}
}

string PWMtoString() {
	ostringstream pwm;
	pwm << "\t\tA\tC\tG\tT\n";
	pwm << "\tA";
	for(unsigned int i=0; i<4; i++) {
		pwm << "\t" << gALIGN_SCORES[(unsigned int)'a'][i];
	}
	pwm << "\n\tC";
	for(unsigned int i=0; i<4; i++) {
		pwm << "\t" << gALIGN_SCORES[(unsigned int)'c'][i];
	}
	pwm << "\n\tG";
	for(unsigned int i=0; i<4; i++) {
		pwm << "\t" << gALIGN_SCORES[(unsigned int)'g'][i];
	}
	pwm << "\n\tT";
	for(unsigned int i=0; i<4; i++) {
		pwm << "\t" << gALIGN_SCORES[(unsigned int)'t'][i];
	}
	pwm << "\n\tN";
	for(unsigned int i=0; i<4; i++) {
		pwm << "\t" << gALIGN_SCORES[(unsigned int)'n'][i];
	}
	pwm << endl;
	
	return pwm.str();
}

void printReadPWM(Read* read) {
	printf("Name: %s\n",read->name);
	printf("Length: %d\n",read->length);
	for(unsigned int i=0; i<read->length; i++) {
		printf("%f %f %f %f\n",read->pwm[i][0],read->pwm[i][1],read->pwm[i][2],read->pwm[i][3]);
	}
}


void sig_handler(int signum) {
	fprintf(stderr,"Process-%d just caught signal %d\n",iproc, signum);

	// Print the backtrace
	void* bt[10];
	size_t size = backtrace(bt,10);
	backtrace_symbols_fd(bt, size, 2);

	exit(0);
}

/* 
 * We'll pull out this code from the main body into a separate function
 */
void setup_matrices() {
	memset(g_gen_CONVERSION,4,256);
	g_gen_CONVERSION[(int)'a'] = g_gen_CONVERSION[(int)'A'] = 0;
	g_gen_CONVERSION[(int)'c'] = g_gen_CONVERSION[(int)'C'] = 1;
	g_gen_CONVERSION[(int)'g'] = g_gen_CONVERSION[(int)'G'] = 2;
	g_gen_CONVERSION[(int)'t'] = g_gen_CONVERSION[(int)'T'] = 3;
	g_gen_CONVERSION[(int)'n'] = g_gen_CONVERSION[(int)'N'] = 4;
	g_gen_CONVERSION[(int)'\n'] = g_gen_CONVERSION[(int)'\r'] = 5;
	// Strange white-space characters
	g_gen_CONVERSION[10] = g_gen_CONVERSION[11] = g_gen_CONVERSION[12] = g_gen_CONVERSION[13] = 5;
	g_gen_CONVERSION[(int)'\0'] = 6;
	g_gen_CONVERSION[(int)'>'] = 7;

	memset(g_bs_CONVERSION,4,256);
	
	g_bs_CONVERSION[(unsigned int)'a'] = g_bs_CONVERSION[(unsigned int)'A'] = 0;
	g_bs_CONVERSION[(unsigned int)'c'] = g_bs_CONVERSION[(unsigned int)'C'] = 1;
	g_bs_CONVERSION[(unsigned int)'g'] = g_bs_CONVERSION[(unsigned int)'G'] = 2;
	g_bs_CONVERSION[(unsigned int)'t'] = g_bs_CONVERSION[(unsigned int)'T'] = 3;
	g_bs_CONVERSION[(unsigned int)'n'] = g_bs_CONVERSION[(unsigned int)'N'] = 4;
	g_bs_CONVERSION[(unsigned int)'\n'] = g_bs_CONVERSION[(unsigned int)'\r'] = 5;
	// Strange white-space characters
	g_bs_CONVERSION[10] = g_bs_CONVERSION[11] = g_bs_CONVERSION[12] = g_bs_CONVERSION[13] = 5;
	g_bs_CONVERSION[(unsigned int)'\0'] = 6;
	g_bs_CONVERSION[(unsigned int)'>'] = 7;
	
	
	memset(gINT2BASE,'?',16);
	gINT2BASE[0] = 'a';
	gINT2BASE[1] = 'c';
	gINT2BASE[2] = 'g';
	gINT2BASE[3] = 't';
	gINT2BASE[4] = 'n';
	gINT2BASE[5] = 'I';
	gINT2BASE[6] = 'D';


#ifdef DEBUG
	printf("int\tbs\tgen\n");
	for(int i=0; i<256; i++) {
		printf("%d\t%c\t%d\t%d\n",i,(char)i,g_bs_CONVERSION[i],g_gen_CONVERSION[i]);
	}
	printf("int2base\n");
	for(int i=0; i<16; i++) {
		printf("%d\t%c\n",i,gINT2BASE[i]);
	}
#endif

}

#include "a_matrices.c"

/**
 * Instead of having each of these be a constant, allocate them according to the number
 * of threads we are requesting.
 */
void alloc_nReads() {
	nReads = READS_PER_PROC * gNUM_THREADS;

	gReadArray = new Read*[nReads];
	gReadLocs = new seq_map[nReads];
	//gReadLocsApprox = new seq_premap[nReads];
	gReadDenominator = new double[nReads];
	gTopReadScore = new double[nReads];

	memset(gReadArray,0,nReads*sizeof(Read*));
	memset(gReadDenominator,0,nReads*sizeof(double));
	memset(gTopReadScore,0,nReads*sizeof(double));
}

/** 
 * Clean up after ourselves
 */
void clean_nReads() {
	delete[] gReadArray;
	delete[] gReadLocs;
	delete[] gReadDenominator;
	delete[] gTopReadScore;
}

int main(const int argc, const char* argv[]) {

#ifdef MPI_RUN
	// If we don't have MPI_THREAD_SERIALIZED, it won't allow us to run with threads
	int provided = MPI::Init_thread((int&)argc, (char**&)argv, MPI_THREAD_SERIALIZED);
	//MPI::Init_thread((int&)argc, (char**&)argv, MPI_THREAD_SERIALIZED);
	///*
	fprintf(stderr,"Provided is %d and MPI_THREAD_SERIALIZED is %d\n",provided,MPI_THREAD_SERIALIZED);
	fprintf(stderr,"Single: %d\n",MPI_THREAD_SINGLE);
	fprintf(stderr,"FUNNELED: %d\n",MPI_THREAD_FUNNELED);
	fprintf(stderr,"SERIALIZED: %d\n",MPI_THREAD_SERIALIZED);
	fprintf(stderr,"MULTIPLE: %d\n",MPI_THREAD_MULTIPLE);
	//*/
	nproc = MPI::COMM_WORLD.Get_size();
	iproc = MPI::COMM_WORLD.Get_rank();
	//fprintf(stderr,"iproc:%d nproc:%d\n",iproc,nproc);
#endif

	double prog_start_time = When();

	// We want to install a sig handler because this is throwing a bus error somewhere...
	struct sigaction bus_action;
	bus_action.sa_handler = sig_handler;
	sigaction(SIGBUS, &bus_action, NULL);

	//Print command line args
	cerr << "This is GNUMAP, Version "gVERSION", for public and private use." << endl;
#ifdef GENOME_STL
	cerr << "# Using STL version to hash.\n";
#else
	cerr << "# Using built-in hashing function.\n";
#endif
	
	// move this code out of the main body.
	setup_matrices();

#ifdef DISCRETIZE
	// Set up the matrices used to discretize the read values
	init_centers();
	init_lookup();
	init_sums();
	//print_stats(stderr);
#endif

	cout << endl << "Command Line Arguments:  ";
	for(int i=0; i<argc; i++)
		cout << argv[i] << ' ';
	cout << endl;
	
	gVERBOSE = 0;

	if(argc == 1) //means only the bin/gnumap parameter
		usage(0,(char*)"");

	if(argc < 2) {
		usage(1, (char*)"Need at least a genome and a file to map.");
		exit(1);
	}

	int rc = ParseCmdLine(argc,argv);
	// an error occurred during option processing
	if(rc != 0) {
		GetParseError(cerr, argv);
		return -1;
	}

	// Move this code out of the main body
	setup_alignment_matrices();

	/* Manage errors that might occur */	
	// If they don't include a fasta file for sequences
	if( (seq_file == NULL) )
		usage(1, (char*)"Specify a single file e.g., sequences.fa\n");
	if(!genome_file)
		usage(1, (char*)"Specify a genome to map to with the -g flag\n");

	if(pos_matrix != NULL) {
		try {
			readPWM(pos_matrix);
		}
		catch(const char* err) {
			cerr << "ERROR: \n\t" << err << endl;
			return -1;
		}
		catch(Exception *e) {
			cerr << "Error: \n\t" << e->GetMessage() << endl;
			delete e;
			return -1;
		}
	}

	// Allocate space for dynammically-allocated arrays
	alloc_nReads();
	
	ostringstream params;
	params << "Parameters: " << endl;
	params << "\tVerbose: " << gVERBOSE << endl;
	if(gREAD)
		params << "\tGenome file to read from: " << gREAD_FN << endl;
	else 
		params << "\tGenome file(s): " << genome_file << endl;
	if(gSAVE)
		params << "\tGenome file to save to: " << gSAVE_FN << endl;
	params << "\tOutput file: " << output_file << endl;
	if(gPRINT_ALL_SAM)
		params << "\t\tPrinting all SAM records\n";
	params << "\tSequence file(s): " << seq_file << endl;
	if(!perc)
		params << "\tAlign score: " << gALIGN_SCORE << endl;
	else
		params << "\tAlign percentage: " << gALIGN_SCORE*100 << "%" << endl;
	if(gCUTOFF_SCORE != 0) {
		if(gCUTOFF_SCORE < 0)
			usage(1, (char*)"-q: Invalid cutoff score.  Must be >= 0");
		params << "\tUsing cutoff score of " << gCUTOFF_SCORE << endl;
	}

#ifdef MPI_RUN
	// You must compile the MPI library to work with multiple threads otherwise
	// it will actually be SLOWER with multiple threads than a single one (it will
	// default to MPI_THREAD_FUNNELED or MPI_THREAD_SINGLE)
	if(provided != MPI_THREAD_SERIALIZED && provided != MPI_THREAD_MULTIPLE) {
		cerr << "\nWarning:  MPI libraries do not support multiple threads. "
			 << "Turning this option off\n\n";
		gNUM_THREADS = 1;
	}
#endif
	params << "\tNumber of threads: " << gNUM_THREADS << endl;
	if(gFAST) {
		if(!gMER_SIZE)	//if the user didn't specify one
			gMER_SIZE = 14;
		//gJUMP_SIZE = gMER_SIZE;
		params << "\tUsing FAST alignment mode" << endl;
	}
	if(gGEN_SKIP != 0) {
		if(gGEN_SKIP < 0)
			usage(1, (char*)"-s: Invalid Genome Skip size\n");
		params << "\tSkipping " << gGEN_SKIP << " bases during hash step" << endl;
	}
	gGEN_SKIP++;	// We're mod'ing in the function, so we want to mod by GEN_SKIP+1
	if(gMAX_HASH_SIZE > 0)
		params << "\tLargest Hash Size: " << gMAX_HASH_SIZE << endl;
	if(!gMER_SIZE)	//if the user didn't specify one
		gMER_SIZE = DEF_MER_SIZE;
	params << "\tMer size: " << gMER_SIZE << endl;

	/*
	if(gJUMP_SIZE) {
		if(gJUMP_SIZE < 1)
			usage(1, (char*)"-j/--jump: Invalid jump size\n");
	}
	else {
		gJUMP_SIZE = gMER_SIZE/2;
	}
	params << "\tUsing jump size of " << gJUMP_SIZE << endl;
	
	
	if(gMIN_JUMP_MATCHES < 1)
		usage(1, (char*)"-k/--top_seed: Invalid matching seed number\n");
	params << "\tUsing min seed matches for each sequence of " << gMIN_JUMP_MATCHES << endl;
	*/
	if(pos_matrix != NULL) {
		params << "\tUsing User-Defined Alignment Scores: " << endl;
		params << PWMtoString();
	}	
	else 
		params << "\tUsing Default Alignment Scores" << endl;
	params << "\tGap score: " << gGAP << endl;
	params << "\tMaximum Gaps: " << gMAX_GAP << endl;
	if(g_adaptor) {
		params << "\tUsing Adaptor Sequence: " << g_adaptor << endl;
	}
	if(gSNP) {
		cerr << "Sorry, can you run this snp analysis at sam2sgr after running gnumap first?" << endl;
		exit(1);
		
		params << "\tEmploying SNP calling" << endl;
		params << "\t  SNP p-value is "<<gSNP_PVAL<<endl;
		if(gSNP_MONOP)
			params << "\t  Only allowing for monoploid SNPs"<<endl;
	}

	if(gBISULFITE) {
		gPRINT_FULL = true;
		params << "\tSensing Methylated Cytosines... " << endl;
		
		gEvalLambda = 0.3274;
		
		if (gMATCH_POS_STRAND_BS) {
			g_bs_CONVERSION[(int)'c'] = g_bs_CONVERSION[(int)'C'] = 3;	//change it so every 'c' will be *hashed* as a 't'
			// If they've supplied a matrix, we don't want to mess with this
			if(pos_matrix == NULL) {
				gALIGN_SCORES[(int)'c'][0] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'c'][1] = gMATCH;
				gALIGN_SCORES[(int)'c'][2] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'c'][3] = 0;
				
				//when it aligns to 'c' of + strand(ref) 
				gALIGN_SCORES[(int)'d'][0] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'d'][1] = 0;
				gALIGN_SCORES[(int)'d'][2] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'d'][3] = gMATCH;
			}
			gNtConvFlag = "ct";
		}
	
		else if (gMATCH_NEG_STRAND_BS) {	// we don't want to match to the positive strand, by default only mapping to one strand
		// when doing the opposite strand, we want to allow 'G->A' (the rev comp)
		// to represent this profile, sam2sgr should be aware of this mapping and it will be a todo!
			g_bs_CONVERSION[(int)'g'] = g_bs_CONVERSION[(int)'G'] = 0;	//change it so every 'g' will be *hashed* as a 'a'
			// If they've supplied a matrix, we don't want to mess with this
			if(pos_matrix == NULL) {
				gALIGN_SCORES[(int)'g'][0] = 0; //we have to turn it off, o.w., max_score to be expected goes higher!
				gALIGN_SCORES[(int)'g'][1] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'g'][2] = gMATCH;
				gALIGN_SCORES[(int)'g'][3] = gTRANSVERSION_bs;
				
				//when it aligns to 'g' of - strand(ref)
				gALIGN_SCORES[(int)'h'][0] = gMATCH;
				gALIGN_SCORES[(int)'h'][1] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'h'][2] = 0;
				gALIGN_SCORES[(int)'h'][3] = gTRANSVERSION_bs;
			}

			gNtConvFlag = "ga";

		}

	}

	if(gATOG) {
		gPRINT_FULL = true;
		params << "\tDetecting A-to-I RNA edit events..." << endl;
	
		/*
		 * We don't want to match to only one strand:  All 4, REW,REWR,REC,RECR
		// RNA editing only works with one strand, so don't let them have both
		if(gMATCH_POS_STRAND && gMATCH_NEG_STRAND)
			usage(1, "Please specify a strand to match with --up_strand or --down_strand");
			// exit
		 */
		gEvalLambda = 0.4073;
		
		if (gMATCH_POS_STRAND_RE) {
			g_bs_CONVERSION[(int)'a'] = g_bs_CONVERSION[(int)'A'] = 2;	//change it so every 'a' will be *hashed* as a 'g'
			// If they've supplied a matrix, we don't want to mess with this
			if(pos_matrix == NULL) {
				gALIGN_SCORES[(int)'a'][0] = gALIGN_SCORES[(int)'A'][0] = gMATCH;
				gALIGN_SCORES[(int)'a'][1] = gALIGN_SCORES[(int)'A'][1] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'a'][2] = gALIGN_SCORES[(int)'A'][2] = 0; //we have to turn it off
				gALIGN_SCORES[(int)'a'][3] = gALIGN_SCORES[(int)'A'][3] = gTRANSVERSION_bs;
				
				//when it aligns to 'a' of + strand
				gALIGN_SCORES[(int)'b'][0] =  0;
				gALIGN_SCORES[(int)'b'][1] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'b'][2] = gMATCH;
				gALIGN_SCORES[(int)'b'][3] = gTRANSVERSION_bs;
			}
			gNtConvFlag = "ag";
		}
		else if (gMATCH_NEG_STRAND_RE) { // only if we don't want to match to both strands...
			// when doing the opposite strand, we want to allow 'T->C' (the rev comp)
			g_bs_CONVERSION[(int)'t'] = g_bs_CONVERSION[(int)'T'] = 1;	//change it so every 't' will be *hashed* as a 'c'
			// If they've supplied a matrix, we don't want to mess with this
			if(pos_matrix == NULL) {
				gALIGN_SCORES[(int)'t'][0] = gALIGN_SCORES[(int)'T'][0] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'t'][1] = gALIGN_SCORES[(int)'T'][1] = 0;
				gALIGN_SCORES[(int)'t'][2] = gALIGN_SCORES[(int)'T'][2] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'t'][3] = gALIGN_SCORES[(int)'T'][3] = gMATCH;
				
				//when it aligns to 'c' of - strand
				gALIGN_SCORES[(int)'u'][0] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'u'][1] = gMATCH;
				gALIGN_SCORES[(int)'u'][2] = gTRANSVERSION_bs;
				gALIGN_SCORES[(int)'u'][3] = 0;
				
			}
			gNtConvFlag = "tc";
		}
	}
	
	if(gGEN_SIZE != 8) {
		if(gGEN_SIZE < 1)
			usage(1,(char*)"Invalid bin size (must be more than 0)\n");
		if(gGEN_SIZE > 8)
			usage(1,(char*)"Invalid bin size (must be 8 or less)\n");
		params << "\tUsing irregular bin size of " << gGEN_SIZE << endl;
	}
	if(!gMATCH_NEG_STRAND) {
		params << "\tOnly matching to positive strand of genome\n";
	}
	if(!gMATCH_POS_STRAND) {
		params << "\tOnly matching to negative strand of genome\n";
	}

	//determine the size of the hash
	gHASH_SIZE = (unsigned int) pow(4.0,gMER_SIZE);

#ifdef DEBUG
	printf("Alignment scores:\n");
	printf("\tA\tC\tG\tT\n");
	printf("A\t%f\t%f\t%f\t%f\n",1.0/gADJUST*gALIGN_SCORES[(int)'a'][0],1.0/gADJUST*gALIGN_SCORES[(int)'a'][1],
			1.0/gADJUST*gALIGN_SCORES[(int)'a'][2],1.0/gADJUST*gALIGN_SCORES[(int)'a'][3]);
	printf("C\t%f\t%f\t%f\t%f\n",1.0/gADJUST*gALIGN_SCORES[(int)'c'][0],1.0/gADJUST*gALIGN_SCORES[(int)'c'][1],
			1.0/gADJUST*gALIGN_SCORES[(int)'c'][2],1.0/gADJUST*gALIGN_SCORES[(int)'c'][3]);
	printf("G\t%f\t%f\t%f\t%f\n",1.0/gADJUST*gALIGN_SCORES[(int)'g'][0],1.0/gADJUST*gALIGN_SCORES[(int)'g'][1],
			1.0/gADJUST*gALIGN_SCORES[(int)'g'][2],1.0/gADJUST*gALIGN_SCORES[(int)'g'][3]);
	printf("T\t%f\t%f\t%f\t%f\n",1.0/gADJUST*gALIGN_SCORES[(int)'t'][0],1.0/gADJUST*gALIGN_SCORES[(int)'t'][1],
			1.0/gADJUST*gALIGN_SCORES[(int)'t'][2],1.0/gADJUST*gALIGN_SCORES[(int)'t'][3]);
	printf("N\t%f\t%f\t%f\t%f\n",1.0/gADJUST*gALIGN_SCORES[(int)'n'][0],1.0/gADJUST*gALIGN_SCORES[(int)'n'][1],
			1.0/gADJUST*gALIGN_SCORES[(int)'n'][2],1.0/gADJUST*gALIGN_SCORES[(int)'n'][3]);
#endif

	if(gVERBOSE) {
		cerr << params.str() << endl;
	}
	
	// This works for MPI and for everything else
	if(nproc > 1) {
		// rename the output file to have the processor number after it
		char* output_ext = new char[strlen(output_file)+10];
		sprintf(output_ext,"%s_%d",output_file,iproc);
		output_file = output_ext;

	}
	char* sam_file = new char[strlen(output_file)+10];
	sprintf(sam_file,"%s.sam",output_file);
                                     
	// Everyone opens up their output file
	of.open(sam_file);
	of.precision(5);
	of << scientific;
	
	// Check to make sure the output file/directory exists
	if(of.bad() || of.fail()) {
		cerr << "ERROR:\n\t";
		perror("Error in reading output file");
		return -1;
	}
	
	// Don't write anything to the output file
	/*
	// only write the params if you're the first node		
	if(iproc == 0) {
		of << "<<HEADER\n";
		of << params.str();
		of << "\nHEADER\n";
	}
	*/

	if(gVERBOSE)
		cerr << "Hashing the genome." << endl;
	// Use the global genome: gGen
	//GENOME_t gen; 
	unsigned long my_start=0, my_end=0;
	
	try {
		unsigned long gen_size=0;

#ifdef MPI_RUN
		// don't need to do this if we're only reading in a file
		if(!gREAD) {
			if(gMPI_LARGEMEM && iproc == 0) {
				GENOME_t notherGen;
				// we don't care about these parameters.  Just read the whole thing
				notherGen.use(genome_file,POS_STRAND);
				gen_size = notherGen.count();
				//fprintf(stderr,"Size of genome here: %lu\n",gen_size);
			}

			//fprintf(stderr,"Size of genome after here: %lu\n",gen_size);
			
			my_start = 0;
			my_end = gen_size;
			// tell each node what part they need to get
			//MPI_Broadcast((char*)&my_start,sizeof(my_start),MPI_CHAR,
			MPI::COMM_WORLD.Bcast(&gen_size, 1, MPI_UNSIGNED_LONG, 0);

			// If we want to have large memory MPI, we should have declared the flag.
			if(gMPI_LARGEMEM) {
				my_start = gen_size/nproc * iproc;
				my_end = my_start + gen_size/nproc;
			}
			//return 0;
			
			fprintf(stderr,"[%d/%d] gen_size=%lu, my_start=%lu, my_end=%lu\n",iproc,nproc,gen_size,my_start,my_end);
		}
#endif

		gGen.use(genome_file, my_start, my_end);
		gGen.LoadGenome();
		gGenSizeBp = gGen.size();
	}
	
	catch(const char* err) {
		cerr << "ERROR: \n\t" << err << endl;
		return -1;
	}
	
	catch(bad_alloc&) {
		perror("ERROR HERE");
		cerr << "ERROR: \n\tUnable to allocate enough memory.\n\t(Pick a smaller mer size?)" << endl;
		cerr << "\t\tTime: " << When()-prog_start_time << endl;
		return -1;
	}
	
	catch(exception &e) {
		cerr << "ERROR: \n\t" << e.what() << endl;
		return -1;
	}
	
	catch(Exception *e) {
		fprintf(stderr,"ERROR: \n\t%s\n",e->GetMessage());
	}
	
	catch(...) {
		cerr << "ERROR: \n\tUnknown error." << endl;
		return -1;
	}

	if(gVERBOSE) 
		fprintf(stderr,"\nTime to hash: %.0f seconds\n",When()-prog_start_time);
		//cerr << "\nTime to hash: " << When()-prog_start_time << endl;
	
	// Don't want to print any header information in this file
	//if(iproc == 0)
		// Output SAM by default
		//of << "# <QNAME>\t<FLAG>\t<RNAME>\t<POS>\t<MAPQ>\t<CIGAR>\t<MRNM>\t<MPOS>\t<ISIZE>\t<SEQ>\t<QUAL>\t<OPT>\n";
	
	gSM = new SeqManager(gReadArray, seq_file, nReads, gNUM_THREADS);
	
	//compute SW matching cutoff
	if (gGenSizeBp0 == 0) {
		gGenSizeBp0 = gGenSizeBp;
	}
	
//we predict a min length of read such that its E-value (approx. local alignment model) is less than 0.1!
	gMinReadL = (unsigned int)ceil(log(EVAL_K*gGenSizeBp0*gSEQ_LENGTH/gNW_Eval_cutoff)/(gMATCH*gEvalLambda/gADJUST));
	
#ifdef DEBUG	
	cerr << "K: " << EVAL_K << "| gGenSizeBp0: " << gGenSizeBp0 << " | gSEQ_LENGTH: " << gSEQ_LENGTH << " | gNW_Eval_cutoff: " << gNW_Eval_cutoff << " | gEvalLambda : " << gEvalLambda << endl;
#endif
	
	cerr << "Minimum Read Length to Process : " << gMinReadL << endl;
	
	
	/*
	if(!iproc)
	{
		int i=0;
		char hostname[256];
		gethostname(hostname, sizeof(hostname));
		fprintf(stderr,"PID %d on %s ready for attach\n",getpid(),hostname);
		while(0 == i)
			sleep(5);
	}
	*/
	
	

	/************************************************************************/
	/* Here is where we'll begin the process of matching and writing to the	*/
	/* output file.															*/
	/************************************************************************/	
	unsigned int seqs_matched = 0;
	unsigned int seqs_not_matched = 0;
	cond_thread_num = gNUM_THREADS;
	setup_complete = true;


	/************************************
	 * Do the threading					*
	 ************************************/
	
	pthread_t pthread_hand[gNUM_THREADS];
	void *pthread_ret[gNUM_THREADS];
	pthread_mutex_init(&lock,NULL);
	pthread_mutex_init(&nwlock,NULL);
	pthread_mutex_init(&write_lock,NULL);
	pthread_mutex_init(&cond_lock,NULL);
	pthread_mutex_init(&total_read_lock,NULL);
	pthread_mutex_init(&comm_barrier_lock,NULL);
	
	
	//Here is where we set the threading information.
	pthread_setconcurrency(gNUM_THREADS);
	pthread_attr_init(&attr);
	pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

	//An array of pointers so we can delete them when we're done.
	thread_opts* t_o_ptr[gNUM_THREADS];
	//Create an array to signify when we're done
	finished_arr = new unsigned int[gNUM_THREADS];
	for(unsigned int i=0; i<gNUM_THREADS; i++) {
		finished_arr[i] = 0;		
	}
	
#ifdef OMP_RUN
	cond_thread_num = 1;
	omp_set_num_threads(gNUM_THREADS);
	thread_opts* t_o = new thread_opts(gGen,*gSM,0);
	thread_rets* ret = omp_thread_run(t_o);

	unsigned int seqs_matched = 0, seqs_not_matched = 0;
	seqs_matched = ret->good_seqs;
	seqs_not_matched = ret->bad_seqs;

#else	// If OMP_RUN not defined
	for(unsigned int i=0; i<gNUM_THREADS; i++) {

		//The parameters to pass to the thread
		thread_opts* t_o = new thread_opts(gGen,*gSM,i);
		t_o_ptr[i] = t_o;

#ifdef MPI_RUN
		if(gMPI_LARGEMEM) 
			pthread_create(&pthread_hand[i], NULL, mpi_thread_run, (void*)t_o);
		else
			pthread_create(&pthread_hand[i], NULL, parallel_thread_run, (void*)t_o);

#else //MPI_RUN not defined
		pthread_create(&pthread_hand[i], NULL, parallel_thread_run, (void*)t_o);
#endif //end MPI_RUN
	}
	
	
	/********************************************************/
	/* wait for all the threads to join back together... 	*/
	/********************************************************/
	for (unsigned int i=0; i<gNUM_THREADS; i++) {
		pthread_join(pthread_hand[i], &pthread_ret[i]);
	}
	
	/********************************************************/
	/* clean up the memory leaks stuff					 	*/
	/********************************************************/
	for(unsigned int i=0; i<gNUM_THREADS; i++) {
		//delete[] seq_ptr[i];
		delete t_o_ptr[i];
	}
	
	for(unsigned int i=0; i<gNUM_THREADS; i++) {
		seqs_matched += ((thread_rets*)pthread_ret[i])->good_seqs;
		seqs_not_matched += ((thread_rets*)pthread_ret[i])->bad_seqs;

		delete (thread_rets*)pthread_ret[i];
	}
#endif //end OMP_RUN
	
	if(gVERBOSE) 
		cerr << endl;
		
	
	if(gVERBOSE)
		cerr << "\n[" << iproc << "/-] Time since start: " << When()-prog_start_time << endl;
	if(gVERBOSE)
		cerr << "\n[" << iproc << "/-] Printing output." << endl;

#ifdef MPI_RUN
	// If it's not the largemem (where we each print out our genome), we need to reduce
	if(!gMPI_LARGEMEM && nproc > 1) {
			
		fprintf(stderr,"[-/%d] Sending genomes...",iproc);
		float* gen_ptr = gGen.GetGenomeAmtPtr();
		unsigned int gen_size = (unsigned int)gGen.size()/gGEN_SIZE;

		/****************************************************************
		 * We only want to create a portion of the genome, bit by bit
		 ****************************************************************/
		//float* sendGenome = (float*)malloc(sizeof(float)*gen_size);
		size_t commGenSize = gen_size < gMEM_BUFFER_SIZE ? gen_size : gMEM_BUFFER_SIZE;
#ifdef INTDISC
		// We need a larger space for sending genomes from the integer discretization method
		float* sendGenome = (float*)calloc(sizeof(float),commGenSize*NUM_SNP_VALS);
		//fprintf(stderr, "%d - Successfully allocated genomes\n", iproc);
#else //INTDISC
		float* sendGenome = (float*)calloc(sizeof(float),commGenSize);
#endif //INTDISC
		if(!sendGenome) {
			fprintf(stderr,"ERROR!!! Could not create genome of size %lu bytes!!!\n",sizeof(float)*commGenSize);
			assert(sendGenome && "sendGenome could not be initialized");
		}
//#ifdef DEBUG
		else {
#ifdef INTDISC
			fprintf(stderr,"Successfully created genome of size %lu bytes\n",sizeof(float)*commGenSize*NUM_SNP_VALS);
#else //INTDISC
			fprintf(stderr,"Successfully created genome of size %lu bytes\n",sizeof(float)*commGenSize);
#endif //INTDISC
		}
//#endif //DEBUG

#ifdef DISCRETIZE
		// Define an MPI datatype to pass
		MPI::Datatype MPIctype = MPI::UNSIGNED_CHAR.Create_contiguous(1);
		MPIctype.Commit();

		// Create the operation for summing centers
		MPI::Op MPImyOp;
		MPImyOp.Init( MPISumCenters, true );
#endif //DISCRETIZE

		//for(size_t i=0; i<gGen.size(); i+=gMEM_BUFFER_SIZE) {
		for(size_t i=0; i<gen_size; i+=gMEM_BUFFER_SIZE) {
			size_t remaining = gen_size - i;
			size_t spots_used = remaining < gMEM_BUFFER_SIZE ?
				remaining : gMEM_BUFFER_SIZE;
#ifdef DEBUG			
			fprintf(stderr,"[%d/%d] For this round, starting at %lu and going for %lu spots, and commGenSize is %lu\n",
					iproc,nproc,i,spots_used,commGenSize*NUM_SNP_VALS);
			fprintf(stderr,"[%d/%d] There are %lu spots remaining, buffer of %u\n",
					iproc,nproc,remaining,gMEM_BUFFER_SIZE);
#endif //DEBUG
			//fprintf(stderr, "[%d] BEF: %f\n", iproc, gen_ptr[500035]);
			memcpy(sendGenome,gen_ptr+i,(size_t)(spots_used*sizeof(float)));
			MPI::COMM_WORLD.Allreduce(sendGenome, gen_ptr+i, spots_used, MPI::FLOAT, MPI_SUM);
			//MPI::COMM_WORLD.Reduce(sendGenome, gen_ptr+i, spots_used, MPI::FLOAT, MPI_SUM, 0);
			//fprintf(stderr, "[%d] AFT: %f\n", iproc, gen_ptr[500035]);

			if(gSNP || gBISULFITE || gATOG) {
#ifdef DISCRETIZE
				if(gVERBOSE > 0)
					fprintf(stderr,"Getting character allocations...\n");
				center_d* read_ptr = gGen.GetGenomeAllotPtr();
				
				// We already have summed our amount pointer, so adjust our weights and then sum them
				//for(unsigned int j=0; j<gen_size; j++) {
				for(unsigned int j=0; j<spots_used; j++) {
					//fprintf(stderr,"[%d: %u] ",iproc,j);
					//char prev = read_ptr[j+i];
					// The previous values exist in sendGenome
					read_ptr[j+i] = adjust_center(sendGenome[j], gen_ptr[j+i], read_ptr[j+i]);
				}
			
				// To reduce, use the space already allocated in sendGenome
				center_d *sendReads = (center_d*)sendGenome;
				memcpy(sendReads, read_ptr+i, spots_used*sizeof(center_d));
				//MPI::COMM_WORLD.Reduce((void*)sendReads, (void*)read_ptr, gen_size, MPIctype, MPImyOp, 0);
				MPI::COMM_WORLD.Reduce((void*)sendReads, (void*)(read_ptr+i), spots_used, MPIctype, MPImyOp, 0);

#elif defined( INTDISC )
				unsigned char* read_ptr = gGen.GetGenomeAllotPtr();
				
				// We already have summed our amount pointer, so adjust our weights and then sum them
				//for(unsigned int j=0; j<gen_size; j++)
				for(unsigned int j=0; j<spots_used; j++)
					adjust255(sendGenome[j], gen_ptr[i+j], read_ptr + (i+j)*NUM_SNP_VALS);
				
				unsigned char* sendReads = (unsigned char*)sendGenome;
				// sizeof(float)=4*sizeof(char), so we can fit all four chars in this one array
				memcpy(sendReads, read_ptr+i*NUM_SNP_VALS, (spots_used*NUM_SNP_VALS)*sizeof(unsigned char));
				//fprintf(stderr, "[%d] BEF: %u %u %u %u %u\n", iproc, read_ptr[500035*NUM_SNP_VALS], read_ptr[500035*NUM_SNP_VALS+1], read_ptr[500035*NUM_SNP_VALS+2], read_ptr[500035*NUM_SNP_VALS+3], read_ptr[500035*NUM_SNP_VALS+4]);
				MPI::COMM_WORLD.Reduce(sendReads, read_ptr+i*NUM_SNP_VALS, spots_used*NUM_SNP_VALS, MPI::UNSIGNED_CHAR, MPI_SUM, 0);
				//fprintf(stderr, "[%d] AFT: %u %u %u %u %u\n", iproc, read_ptr[500035*NUM_SNP_VALS], read_ptr[500035*NUM_SNP_VALS+1], read_ptr[500035*NUM_SNP_VALS+2], read_ptr[500035*NUM_SNP_VALS+3], read_ptr[500035*NUM_SNP_VALS+4]);
#else  //not defined INTDISC or DISCRETIZED
				if(gVERBOSE > 0) 
					fprintf(stderr,"A...");
				// Do the read A's now
				gen_ptr = gGen.GetGenomeAPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
				if(gVERBOSE > 0) 
					fprintf(stderr,"C...");
				// C's
				gen_ptr = gGen.GetGenomeCPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
				if(gVERBOSE > 0) 
					fprintf(stderr,"G...");
				// G's
				gen_ptr = gGen.GetGenomeGPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
				if(gVERBOSE > 0) 
					fprintf(stderr,"T...");
				// T's
				gen_ptr = gGen.GetGenomeTPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
				if(gVERBOSE > 0) 
					fprintf(stderr,"N...");
				// N's
				gen_ptr = gGen.GetGenomeNPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
#ifdef _INDEL
				if(gVERBOSE > 0) 
					fprintf(stderr,"Insertion...");
				// Insertions's
				gen_ptr = gGen.GetGenomeIPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
				if(gVERBOSE > 0) 
					fprintf(stderr,"Deletion...");
				// Deletions's
				gen_ptr = gGen.GetGenomeDPtr();
				if(gen_ptr) {
					memcpy(sendGenome,&gen_ptr[i],spots_used*sizeof(float));
					MPI::COMM_WORLD.Reduce(sendGenome, &gen_ptr[i], spots_used, MPI::FLOAT, MPI_SUM, 0);
				}
#endif // _INDEL
#endif // DISCRETIZE/INTDISC/NONE

			}
		}
		
		fprintf(stderr,"\n[-/%d] Finished!  Printing final .sgr/.gmp file\n",iproc);
		// Then print out our new genome
		if ((gSAM2GMP) && (iproc == 0)) //added by CJ to select an option of creating gmp file, 04/23/2012
			gGen.PrintFinal(output_file);

#ifdef DISCRETIZE
		MPImyOp.Free();
		MPIctype.Free();
#endif
			
		free(sendGenome);
	}
	else
		// Even if we're doing MPI, have everyone print to their (separate) output files
		if (gSAM2GMP) //added by CJ to select an option of creating gmp file, 04/23/2012
			gGen.PrintFinal(output_file);
		
#else //MPI_RUN
	if (gSAM2GMP) //added by CJ to select an option of creating gmp file, 04/23/2012
		gGen.PrintFinal(output_file);
#endif //MPI_RUN

#ifdef MPI_RUN
	//fprintf(stderr,"[-/%d] Reducing seqs_matched from [%u] and not matched from [%u]...\n",iproc,seqs_matched,seqs_not_matched);
	unsigned int bseqs_matched = seqs_matched;
	unsigned int bseqs_not_matched = seqs_not_matched;
	if(gMPI_LARGEMEM) {
		// This won't be accuracte, but we'll just get the best matches and return them
		MPI::COMM_WORLD.Reduce(&bseqs_matched, &seqs_matched, 1, MPI_UNSIGNED, MPI_MAX, 0);
		MPI::COMM_WORLD.Reduce(&bseqs_not_matched, &seqs_not_matched, 1, MPI_UNSIGNED, MPI_MIN, 0);
	}
	else {
		// Here, we DO want to get a sum over all the matches
		MPI::COMM_WORLD.Reduce(&bseqs_matched, &seqs_matched, 1, MPI_UNSIGNED, MPI_SUM, 0);
		MPI::COMM_WORLD.Reduce(&bseqs_not_matched, &seqs_not_matched, 1, MPI_UNSIGNED, MPI_SUM, 0);
	}
	//fprintf(stderr,"[-/%d] After reduction, seqs_matched=[%u] and not matched=[%u]\n",iproc,seqs_matched,seqs_not_matched);
	
#endif //MPI_RUN

	if(iproc == 0) {
	
		double time_prog_end = When();
		
		ostringstream stats;
		stats 	<< "\n#Finished.\n";
		stats	<< "#\tTotal Time: " << time_prog_end-prog_start_time << " seconds.\n";
#ifdef DEBUG_TIME
		stats   << "#\tTime spent waiting: " << cond_lock_time << " + " << write_lock_time << "=" 
											<< cond_lock_time+write_lock_time << " seconds.\n";
#endif //DEBUG_TIME
		stats	<< "#\tFound " << seqs_matched+seqs_not_matched << " sequences.\n";
		stats	<< "#\tSequences matched: " << seqs_matched << "\n";
		stats	<< "#\tSequences not matched: " << seqs_not_matched << "\n";
		stats	<< "#\tOutput written to " << sam_file << "\n";
		
		cerr << stats.str();
		// Don't print stats to output file
		//of << stats.str();		
	}

	of.close();
#ifdef DEBUG
	fprintf(stderr, "[%d/-] Freeing memory...\n", iproc);
#endif //DEBUG
	if(!gMPI_LARGEMEM)
		delete gSM;
	
	delete[] finished_arr;
	delete[] sam_file;
/*
	for (int i=0; i<numHash; i++) {
		delete[] gGen[i];
	}
	delete gGen;
*/	
	// Deallocate space requested for these arrays
#ifdef DEBUG
	fprintf(stderr, "[%d/-] Cleaning reads...\n", iproc);
#endif
	clean_nReads();
	
#ifdef DEBUG		
	fprintf(stderr,"[%d/-] End of program...waiting...\n",iproc);
#endif
	
#ifdef MPI_RUN
	MPI::Finalize();
#endif

	return 0;
}


void comm_cond_wait() {
	// We only want to wait in here if we need to communicate anything.  Otherwise, just continue
	if(!gMPI_LARGEMEM)
		return;
		
#ifdef MPI_RUN
#ifdef DEBUG_TIME
	double begin_time = When();
#endif

	MUTEX_LOCK(&cond_lock);

#ifdef DEBUG_TIME
	double end_time = When();
	cond_lock_time += end_time-begin_time;
#endif

	if(++cond_count < cond_thread_num) {
		//fprintf(stderr,"Thread %d/%d waiting...\n",cond_count,cond_thread_num);
		
#ifdef DEBUG_TIME		
		begin_time = When();
#endif

		pthread_cond_wait(&comm_barrier_cond, &cond_lock);

#ifdef DEBUG_TIME
		end_time = When();
		cond_lock_time += end_time-begin_time;
#endif
		
		MUTEX_UNLOCK(&cond_lock);
		
	}
	else {
		//fprintf(stderr,"[%d/%d] Thread %d/%d made it in!\n",iproc,nproc,cond_count,cond_thread_num);
		// Do your stuff

		// MPI communicate everything you need to		
#ifdef DEBUG_TIME		
		double start_time = When();
		// set the global variable to be my start time
		gTimeBegin = start_time;
#endif
		
		// Do a reduce-all with a SUM on the denominators
		double sendDenominator[nReads];
		memcpy(sendDenominator,gReadDenominator,nReads*sizeof(double));

		MPI::COMM_WORLD.Allreduce(sendDenominator, gReadDenominator, nReads,
					MPI::DOUBLE, MPI_SUM);

#ifdef DEBUG_TIME
		double end_time = When();
#endif
#ifdef DEBUG_NW
		unsigned int total_nw = 0;
		unsigned int max_nw = 0;
		unsigned int min_nw = num_nw[0];

		for(int i=0; i<nproc; i++) {
			total_nw += num_nw[i];
			if(num_nw[i] > max_nw)
				max_nw = num_nw[i];
			if(num_nw[i] < min_nw)
				min_nw = num_nw[i];
		}
		fprintf(stderr,"[%d] Total time for Denoms is %f.  Total NW: %u, Max NW: %u, Min NW: %u, DIFF: %u\n",
				iproc,end_time-start_time, total_nw,max_nw,min_nw,max_nw-min_nw);
#endif		
#ifdef DEBUG_TIME
		start_time = When();
#endif
		
		// Do a reduce-all with a MAX on the top reads
		double sendReads[nReads];
		memcpy(sendReads, gTopReadScore, nReads*sizeof(double));

		MPI::COMM_WORLD.Allreduce(sendReads, gTopReadScore, nReads,
					MPI::DOUBLE, MPI_MAX);
#ifdef DEBUG_TIME
		end_time = When();
		fprintf(stderr,"[%d] Total time for TopReads is %f\n",iproc,end_time-start_time);
#endif

		cond_count = 0;
		MUTEX_UNLOCK(&cond_lock);
		pthread_cond_broadcast(&comm_barrier_cond);
	}
#endif
}

void write_cond_wait() {
#ifdef DEBUG_TIME
	double begin_time = When();
#endif
	MUTEX_LOCK(&cond_lock);
#ifdef DEBUG_TIME
	double end_time = When();
	cond_lock_time += end_time-begin_time;
#endif
	if(++cond_count < cond_thread_num) {
		//fprintf(stderr,"Thread %d/%d waiting...\n",cond_count,cond_thread_num);
#ifdef DEBUG_TIME
		begin_time = When();
#endif
		pthread_cond_wait(&comm_barrier_cond, &cond_lock);
#ifdef DEBUG_TIME
		end_time = When();
		cond_lock_time += end_time-begin_time;
#endif
		
		MUTEX_UNLOCK(&cond_lock);
	}
	else {
		//fprintf(stderr,"Thread %d/%d made it in!\n",cond_count,cond_thread_num);

#ifdef MPI_RUN
		if(gMPI_LARGEMEM) {

			//fprintf(stderr,"\nSize of vector: %u\n",gTopReadOutput.size());
			unsigned int total_outputed = 0;
			
			// Each machine prints out their own reads
			
#ifdef DEBUG_TIME
			begin_time = When();
#endif
			// Lock on the vector so others don't use it
			MUTEX_LOCK(&write_lock);
#ifdef DEBUG_TIME
			end_time = When();
			write_lock_time += end_time-begin_time;
#endif
			vector<TopReadOutput>::iterator vit;
			for(vit = gTopReadOutput.begin(); vit != gTopReadOutput.end(); vit++) {
		// Means it's a valid match
				of << (*vit).READ_NAME << "\t";
				if((*vit).MAPQ >= 0) {
					total_outputed++;
					if((*vit).strand == POS_UNIQUE)
						of << 0x000 << "\t";
					else if((*vit).strand == POS_MULTIPLE)
						of << 0x100 << "\t";
					else if((*vit).strand == NEG_UNIQUE)
						of << 0x010 << "\t";					
					else if((*vit).strand == NEG_MULTIPLE)
						of << 0x110 << "\t";

					of << (*vit).CHR_NAME << "\t" << (*vit).CHR_POS << "\t";
					of << (*vit).MAPQ << "\t";
				}
				else {
					of << 0x0200 << "\t";
					of << (*vit).CHR_NAME << "\t" << (*vit).CHR_POS << "\t";
					of << "255\t";
				}

				of << (*vit).CIGAR << "\t";
				of << "*\t0\t0\t";
				of << (*vit).consensus << "\t";
				of << (*vit).qual << "\t";	

				// We're adjusting the alignment score internally.  The math works, but it's not
				// intuitive unless we adjust the score back when we print it out
				of << "AS:i:" << (int)floor(((*vit).A_SCORE)*4.0+0.5) << "\t";
				of << "XP:f:" << (float)((*vit).POST_PROB) << "\t";
				of << "X0:i:" << (*vit).SIM_MATCHES << "\t";
				of.precision(3);
				of << scientific << "XD:f:" << (float)((*vit).DENOM) << "\t";
				of << "XS:Z:" << gNtConvFlag << "\n";
			}

			// Clear the output vector
			gTopReadOutput.clear();
			MUTEX_UNLOCK(&write_lock);
				
		
		} // endif(gMPI_LARGEMEM)
		else {
			// Write it all to the file 
#ifdef DEBUG_TIME
			begin_time = When();
#endif
			MUTEX_LOCK(&write_lock);
#ifdef DEBUG_TIME
			end_time = When();
			write_lock_time += end_time-begin_time;
#endif
			vector<TopReadOutput>::iterator vit;
			for(vit = gTopReadOutput.begin(); vit != gTopReadOutput.end(); vit++) {
		// Means it's a valid match

				of << (*vit).READ_NAME << "\t";
				if((*vit).MAPQ >= 0) {

					if((*vit).strand == POS_UNIQUE)
						of << 0x000 << "\t";
					else if((*vit).strand == POS_MULTIPLE)
						of << 0x100 << "\t";
					else if((*vit).strand == NEG_UNIQUE)
						of << 0x010 << "\t";					
					else if((*vit).strand == NEG_MULTIPLE)
						of << 0x110 << "\t";

					of << (*vit).CHR_NAME << "\t" << (*vit).CHR_POS << "\t";
					of << (*vit).MAPQ << "\t";
				}
				else {
					of << 0x0200 << "\t";
					of << (*vit).CHR_NAME << "\t" << (*vit).CHR_POS << "\t";
					of << "255\t";
				}

				of << (*vit).CIGAR << "\t";
				of << "*\t0\t0\t";
				of << (*vit).consensus << "\t";
				of << (*vit).qual << "\t";	

				// We're adjusting the alignment score internally.  The math works, but it's not
				// intuitive unless we adjust the score back when we print it out
				of << "AS:i:" << (int)floor(((*vit).A_SCORE)*4.0+0.5) << "\t";
				of << "XP:f:" << (float)((*vit).POST_PROB) << "\t";
				of << "X0:i:" << (*vit).SIM_MATCHES << "\t";
				of.precision(3);
				of << scientific << "XD:f:" << (float)((*vit).DENOM) << "\t";
				of << "XS:Z:" << gNtConvFlag << "\n";
			}
			// Clear the output vector
			gTopReadOutput.clear();
			MUTEX_UNLOCK(&write_lock);
		}
#endif
		
		cond_count = 0;
		MUTEX_UNLOCK(&cond_lock);
		pthread_cond_broadcast(&comm_barrier_cond);
	}// endelse(++cond_count < cond_thread_num)
}


void single_write_cond_wait(int thread_no) {
	
	//fprintf(stderr,"[%d] Thread %d made it in!\n",thread_no,cond_count);
#ifdef DEBUG_TIME
	double wait_begin = When();
#endif
	MUTEX_LOCK(&write_lock);
#ifdef DEBUG_TIME
	double wait_end = When();
	write_lock_time += wait_end - wait_begin;
#endif

	// Write it all to the file 
	vector<TopReadOutput>::iterator vit;
	for(vit = gTopReadOutput.begin(); vit != gTopReadOutput.end(); vit++) {
		// Means it's a valid match

		of << (*vit).READ_NAME << "\t";
		if((*vit).MAPQ >= 0) {

			if((*vit).strand == POS_UNIQUE)
				of << 0x000 << "\t";
			else if((*vit).strand == POS_MULTIPLE)
				of << 0x100 << "\t";
			else if((*vit).strand == NEG_UNIQUE)
				of << 0x010 << "\t";					
			else if((*vit).strand == NEG_MULTIPLE)
				of << 0x110 << "\t";

			of << (*vit).CHR_NAME << "\t" << (*vit).CHR_POS << "\t";
			of << (*vit).MAPQ << "\t";
		}
		else {
			of << 0x0200 << "\t";
			of << (*vit).CHR_NAME << "\t" << (*vit).CHR_POS << "\t";
			of << "255\t";
		}

		of << (*vit).CIGAR << "\t";
		of << "*\t0\t0\t";
		of << (*vit).consensus << "\t";
		of << (*vit).qual << "\t";	

		// We're adjusting the alignment score internally.  The math works, but it's not
		// intuitive unless we adjust the score back when we print it out
		of << "AS:i:" << (int)floor(((*vit).A_SCORE)*4.0+0.5) << "\t";
		of << "XP:f:" << (float)((*vit).POST_PROB) << "\t";
		of << "X0:i:" << (*vit).SIM_MATCHES << "\t";
		of.precision(3);
		of << scientific << "XD:f:" << (float)((*vit).DENOM) << "\t";
		of << "XS:Z:" << gNtConvFlag << "\n";
	}
	
	// Clear the output vector
	gTopReadOutput.clear();
	
	MUTEX_UNLOCK(&write_lock);
}


// We just want to make sure we're clean before we delete all the reads...
void clean_cond_wait(bool my_finished) {
#ifdef DEBUG_TIME
	double begin_time = When();
#endif
	MUTEX_LOCK(&cond_lock);
#ifdef DEBUG_TIME
	double end_time = When();
	cond_lock_time += end_time-begin_time;
#endif

	finished += my_finished;

	if(++cond_count < cond_thread_num) {
#ifdef DEBUG_TIME
		begin_time = When();
#endif
		pthread_cond_wait(&comm_barrier_cond, &cond_lock);
#ifdef DEBUG_TIME
		end_time = When();
		cond_lock_time += end_time-begin_time;
#endif
		
		MUTEX_UNLOCK(&cond_lock);
		
	}
	else {
		//fprintf(stderr,"Exiting the output thingy\n");
		// Now that we're done, reset the global SeqManager's edit point
		gSM->resetCounter();
		gReadsDone = 0;
		iter_num++;
		
		cond_count = 0;
		pthread_cond_broadcast(&comm_barrier_cond);
		MUTEX_UNLOCK(&cond_lock);
	}
}

void single_clean_cond_wait(bool my_finished, int thread_no, bool verbose) {
	
	if(!my_finished && thread_no)
		return;

#ifdef DEBUG_TIME
	double begin_time = When();
#endif
	MUTEX_LOCK(&cond_lock);
#ifdef DEBUG_TIME
	double end_time = When();
	cond_lock_time += end_time-begin_time;
#endif
	
	finished_arr[thread_no] = my_finished;

	// Only have one thread check
	if(thread_no == 0) {
		unsigned int total = 0;
		// We want to make sure every thread has checked in here to verify that they're done.
		for(unsigned int i=0; i<gNUM_THREADS; i++) {
			total += finished_arr[i];
		}
		if(total == gNUM_THREADS)
			finished = true;
			
		iter_num++;
		if(verbose)
			gSM->resetCounter();
			
	}
	MUTEX_UNLOCK(&cond_lock);
}

/**
 * Thread runner for non-MPI required runs
 * This is NOT MPI_largemem.
 */
void* parallel_thread_run(void* t_opts) {

	//cout << "New Thread\n";
	unsigned int thread_id = ((thread_opts*)t_opts)->thread_id;

	unsigned int numRPT = READS_PER_PROC;

	unsigned int read_begin = numRPT*thread_id;
	unsigned int read_end = read_begin + numRPT;

	unsigned int good_seqs = 0;
	unsigned int bad_seqs = 0;

	unsigned int seqs_processed = 0;
	
	bool my_finished = false;
		
	// Fill the read buffer
	while(!my_finished) {

		//fprintf(stdout,"[%d/%d at %d] Thread continuing, finshed=%d, mine=%d\n",
		//		iproc,thread_id,iter_num,finished,my_finished);

		//fprintf(stderr,"[%d/%d] Getting more reads from %u to %u\n",thread_id,iproc,read_begin,read_end);
		my_finished = !gSM->getMoreReads(read_begin,read_end,false);
		//fprintf(stderr,"[%d/%d] Got more reads successfully? %s from %d to %d\n",
		//		thread_id,iproc,!my_finished ? "Y" : "**N**",read_begin,read_end);


		for(unsigned int k=read_begin; k<read_end; k++) {
			//vector<vector<double> > temp_vect = ((thread_opts*)t_opts)->seqs[k];
			Read* temp_read = gReadArray[k];
			if(!temp_read)
				break;
			
			string consensus = GetConsensus(*temp_read);
			
#ifdef DEBUG
			cout << "readID: " << temp_read->name << endl;
#endif
			set_top_matches(((thread_opts*)t_opts)->gen, k, consensus, bad_seqs, good_seqs, thread_id);
			
			seqs_processed++;
			
		}  //end for loop
		
		// We'll break these two loops up so they can be doing different work instead of
		// getting caught up on all the mutex locks.
		for(unsigned int k=read_begin; k<read_end; k++) {
			if(!gReadArray[k])
				break;
			
			string consensus = GetConsensus(*(gReadArray[k]));
			
			create_match_output(((thread_opts*)t_opts)->gen, k, consensus);
			
			delete_read(gReadArray[k]);
			gReadArray[k] = 0;
		}
		
		// This just sends one processor to print everything out
		if(thread_id == 0)
			single_write_cond_wait(thread_id);
			
		//fprintf(stdout,"[%d/%d] Before Clean cond wait and cond_thread_num=%d\n",iproc,thread_id,cond_thread_num);
		//double s_time = When();
		single_clean_cond_wait(my_finished, thread_id, true);
		//double e_time = When();
		//fprintf(stderr,"[%d/%d] Total wait time for clean_cond is %f\n",
		//		iproc,thread_id,e_time-s_time);
		//fprintf(stderr,"[%d/%d at %d] After Clean cond wait and cond_thread_num=%d and my_finished=%d, finished=%d\n",
		//			iproc,thread_id,iter_num,cond_thread_num,my_finished,finished);
		
	} // end while(!my_finished)
		
	// Make sure we wait for everyone to finish here
	if(thread_id == 0)
		while(!finished)
			single_clean_cond_wait(my_finished, thread_id, false);

	//fprintf(stderr,"[%d/%d] getting ready to enter single_write_cond_wait...\n",iproc,thread_id);
	// Print them out here, just to make sure
	if(thread_id == 0)
		single_write_cond_wait(thread_id);

	//fprintf(stderr,"[%d/%d] Finished after processing %d/%u reads with %u good and %u bad\n",iproc,thread_id,seqs_processed,gSM->getTotalSeqCount(),good_seqs,bad_seqs);
	
	struct thread_rets* ret = new thread_rets;
	ret->good_seqs = good_seqs;
	ret->bad_seqs = bad_seqs;
	
	return (void*)ret;
}

/**
 * This is just a block of code, not really a function call.  We just want to put everything in here...
 * 
 * By including the flag -DOMP_RUN, this function will use openmp to split up the reads.  Just for testing
 * to see if I could do better (and I can)
 */
#ifdef OMP_RUN
thread_rets* omp_thread_run(thread_opts* t_opts) {
	//fprintf(stderr,"CALLING OMP_THREAD_RUN\n");

	unsigned int i,j, nReadsRead, good_seqs=0, bad_seqs=0;
#ifdef DEBUG_TIME
	double total_time=0, comm_time=0;
#endif

#pragma omp parallel shared(t_opts,gReadArray,gReadDenominator,gTopReadScore)
{
	while(!finished) {

#ifdef DEBUG_NW
		fprintf(stderr,"[%d/%d] Num nw's: %u\n",iproc,omp_get_thread_num(),num_nw[omp_get_thread_num()]);
		num_nw[omp_get_thread_num()] = 0;
#endif

#pragma omp single
{
		t_opts->sm.fillReadArray(gReadArray,nReads,nReadsRead);
		fprintf(stderr,"[-/%d] Num Reads to read: %u and found %u.  Finished is %d\n",iproc,nReads,nReadsRead,finished);

}

		// Use omp to parallelize this code
#pragma omp for private(i) schedule(dynamic,128) reduction(+:good_seqs) reduction(+:bad_seqs)
		for(i=0; i<nReadsRead; i++) {
			//fprintf(stderr,"[%d:%d] Setting match output for %d\n",iproc,omp_get_thread_num(),i);
			string consensus = GetConsensus(*(gReadArray[i]));
			unsigned int thread_no = omp_get_thread_num();
			set_top_matches(t_opts->gen, i, consensus, bad_seqs, good_seqs, thread_no);
		}

// wait for all the threads to sync
#pragma omp single
{
#ifdef DEBUG_TIME
		double start_comm = When();
#endif
		comm_cond_wait();
#ifdef DEBUG_TIME
		comm_time += When()-start_comm;
		fprintf(stderr,"[%d] Made it past the comm_cond_wait: %f\n",iproc,When()-start_comm);
#endif
}

#pragma omp for private(i) schedule(dynamic,128)
		for(i=0; i<nReadsRead; i++) {
			//fprintf(stderr,"[%d:%d] Creating match output for %d\n",iproc,omp_get_thread_num(),i);
			string consensus = GetConsensus(*(gReadArray[i]));
			create_match_output(t_opts->gen, i, consensus);
		}

#pragma omp single
{
#ifdef DEBUG_TIME
		double start = When();
#endif
		write_cond_wait();
#ifdef DEBUG_TIME
		total_time += When()-start;
		fprintf(stderr,"[%d] Made it past the write_cond_wait: %f\n",iproc,When()-start);
#endif
}

#pragma omp for private(i,j) schedule(static)
		for(i=0; i<nReadsRead; i++) {
			if(!gReadArray[i])
				continue;

			for(j=0; j<gReadArray[i]->length; j++)
				delete[] gReadArray[i]->pwm[j];

			delete[] gReadArray[i]->pwm;
			if(gReadArray[i]->name)
				delete[] gReadArray[i]->name;

			delete gReadArray[i];
			gReadArray[i] = 0;
			gReadDenominator[i] = 0;
			gTopReadScore[i] = 0;
		}

#pragma omp single
{
		gSM->printProgress();
#ifdef DEBUG_TIME
		fprintf(stderr,"[-/%d] Seconds used in write_comm: %f\tcomm_cond: %f\n",iproc,total_time,comm_time);
#endif
		finished = gSM->isFinished();
}


	}
}
	
#ifdef DEBUG_TIME
	fprintf(stderr,"[-/%d] Total time used in write_comm: %f\tcomm_cond: %f\n",iproc,total_time,comm_time);
#endif
	
	struct thread_rets* ret = new thread_rets;
	ret->good_seqs = good_seqs;
	ret->bad_seqs = bad_seqs;
	return ret;
}

#endif

/**
 * This function will be used by each thread when the MPI_largmem flag is used.
 */
void* mpi_thread_run(void* t_opts) {
	//fprintf(stderr,"CALLING MPI_THREAD_RUN\n");

	while(!setup_complete);	// Busy-Wait for the setup to complete
	
	unsigned int thread_id = ((thread_opts*)t_opts)->thread_id;
	unsigned int i,j,begin=0,end=0;
	unsigned int bad_seqs=0, good_seqs=0;
	
	bool my_finished = false;
		
	// main loop body
	while(!finished) {

#ifdef DEBUG_NW
		// For testing, reset the number of NW's we're doing
		num_nw[thread_id] = 0;
#endif
		 
		// We'll be looping until we're finished with everything.  
		//Just read a portion at a time
		MUTEX_LOCK(&total_read_lock);
		while(gReadsDone < nReads && !my_finished) {
		
			// Get your portion of the reads here
			// It's locking, so we don't need to worry about multi-entrances
			//my_finished = !((thread_opts*)t_opts)->sm.getMoreReads(begin, end, true);
			my_finished = !gSM->getMoreReads(begin, end, true);

			gReadsDone += end-begin;
			MUTEX_UNLOCK(&total_read_lock);


			//fprintf(stderr,"[%u/%u] Thread %u has seqs %u-%u\n",iproc,thread_id,thread_id,begin,end);
			// Do the first part of the alignment
			unsigned int count = 0;
			for(i=begin; i<end; i++, count++) {
	
				string consensus = GetConsensus(*(gReadArray[i]));
				set_top_matches(((thread_opts*)t_opts)->gen, i, consensus, bad_seqs, good_seqs, thread_id);
	
				// For some reason, the percentage complete doesn't work...
				//if(gVERBOSE)
				//	fprintf(stderr,"\r%d%% complete",(int)(((double)i)/gSM->getNumSeqsPProc())*100);
	
			}
			
			MUTEX_LOCK(&total_read_lock);
		} // End of reading portions
		MUTEX_UNLOCK(&total_read_lock);
		

		//fprintf(stderr,"[%d/%d] Thread %d about to enter cond_wait\n",iproc,nproc,((thread_opts*)t_opts)->thread_id);
		// Wait for each thread to finish
#ifdef DEBUG_TIME
		double b_time = When();
#endif
		comm_cond_wait();
#ifdef DEBUG_TIME
		double e_time = When();
#endif

#ifdef DEBUG_NW
		fprintf(stderr,"%u - [%d/%d] Total time for comm_wait: %f, since start: %f, difference: %f, num_nw: %u\n",
				iter_num,iproc,thread_id, e_time-b_time, gTimeBegin-b_time, e_time-gTimeBegin,num_nw[thread_id]);
		//fprintf(stderr,"Thread %d just exited cond_wait\n",((thread_opts*)t_opts)->thread_id);
#elif defined(DEBUG_TIME)
		fprintf(stderr,"%u - [%d/%d] Total time for comm_wait: %f, since start: %f, difference: %f\n",
				iter_num,iproc,thread_id, e_time-b_time, gTimeBegin-b_time, e_time-gTimeBegin);
#endif

		// Now just set the begin and end to be an equivalent portion of reads
		unsigned int readsPerProc = nReads/gNUM_THREADS;
		begin = readsPerProc*thread_id;
		end = readsPerProc*(thread_id+1);
		end = (end > nReads) ? nReads : end;
		
		for(i=begin; i<end; i++) {
				
			// We'll just continue if it's empty
			if(!gReadArray[i])
				continue;

			string consensus = GetConsensus(*(gReadArray[i]));
			create_match_output(((thread_opts*)t_opts)->gen, i, consensus);
		}

		//fprintf(stderr,"Thread %d about to enter write cond_wait\n",((thread_opts*)t_opts)->thread_id);
		// Wait for each thread to finish, then write them.
#ifdef DEBUG_TIME
		b_time = When();
#endif
		write_cond_wait();
#ifdef DEBUG_TIME
		e_time = When();
		fprintf(stderr,"[%d/%d] Total time for write_wait: %f\n", iproc,thread_id, e_time-b_time);
#endif
		
		// Delete the reads you've used
		for(i=begin; i<end; i++) {
			if(!gReadArray[i])
				continue;
			
			for(j=0; j<gReadArray[i]->length; j++) {
				delete[] gReadArray[i]->pwm[j];
			}
			delete[] gReadArray[i]->pwm;
			if(gReadArray[i]->name)
				delete[] gReadArray[i]->name;

			delete gReadArray[i];
			gReadArray[i] = 0;
			gReadDenominator[i] = 0;
			gTopReadScore[i] = 0;
			
			//clearMapAt(i);
		}
		
		// Even if we're not using MPI, we should wait here.
		clean_cond_wait(my_finished);
		//fprintf(stderr,"Thread %d finished, starting over\n",((thread_opts*)t_opts)->thread_id);
	}
	
	struct thread_rets* ret = new thread_rets;
	ret->good_seqs = good_seqs;
	ret->bad_seqs = bad_seqs;
	
	return (void*)ret;
}


/************************************************************
 * COMMAND-LINE STUFF                                       *
 ************************************************************/

int ParseCmdLine(const int argc, const char* argv[]) {
	
	bool found_other = false;

	for(int i=1; i<argc; i++) {
		if(*argv[i] == '-') { //check for normal
			int set_ret;

			if(*((argv[i])+1) == '-')  { //check for extended
				//printf("Found extended: %s\n",argv[i]);
				set_ret = set_arg_ext(argv[i],argv[i+1],i);
			}
			else if(strlen(argv[i]) > 2) {
				set_ret = PARSE_ERROR;
			}
			else {
				set_ret = set_arg(argv[i],argv[i+1],i);
				i++;	//increment past the next expression
			}
			
			if(set_ret != 0) {
				this_cmd.errnum = set_ret;
				this_cmd.errpos = i;
				return -1;
			}

		}
		else {
			//if we've already found the the other cmd arg, it doesn't work. 
			if(found_other) {
				this_cmd.errnum = INVALID_ARG;
				this_cmd.errpos = i;
				return INVALID_ARG;
			}
			else {	//set the other arg to this pointer
				seq_file = argv[i];
			}
		}
	}
	
	if (gATOG) {
		if (gMATCH_POS_STRAND) {
			gMATCH_POS_STRAND_RE = true;
			gMATCH_NEG_STRAND_RE = false;
		}
		else if (gMATCH_NEG_STRAND) {
			gMATCH_NEG_STRAND_RE = true;
			gMATCH_POS_STRAND_RE = false;
		}
		gMATCH_POS_STRAND = true;
		gMATCH_NEG_STRAND = true;
	}

	if (gBISULFITE) {
		if (gMATCH_POS_STRAND) {
			gMATCH_POS_STRAND_BS = true;
			gMATCH_NEG_STRAND_BS = false;
		}
		else if (gMATCH_NEG_STRAND) {
			gMATCH_NEG_STRAND_BS = true;
			gMATCH_POS_STRAND_BS = false;
		}
		gMATCH_POS_STRAND = true;
		gMATCH_NEG_STRAND = true;
	}
	
	gMAX_PRE_MATCHES_HALF = PRE_MATCH_BACKUP_FOLDS_STRAND * gMAX_MATCHES;
	
// 	if (!gMATCH_POS_STRAND || !gMATCH_NEG_STRAND) {
// 		gMAX_PRE_MATCHES_HALF = 2 * PRE_MATCH_BACKUP_FOLDS_STRAND * gMAX_MATCHES;
// 	}
// 	else {
// 		gMAX_PRE_MATCHES_HALF = PRE_MATCH_BACKUP_FOLDS_STRAND * gMAX_MATCHES;
// 	}
	return 0;

}

void GetParseError(ostream &os, const char* argv[]) {
	if(this_cmd.errnum == PARSE_ERROR) {
		os << "Irregular Parameter in: " << argv[this_cmd.errpos] << endl;
	}
	else if(this_cmd.errnum == NO_MATCHING_ARG) {
		os << "No matching arg in: " << argv[this_cmd.errpos] << endl;
	}
	else if(this_cmd.errnum == INVALID_ARG) {
		os << "Please specify only a single sequence file, or multiple files in \"quotations\" ("
		   << argv[this_cmd.errpos] << ")" << endl;
	}
	else {
		os << "Unknown error " << this_cmd.errnum << " in: " << argv[this_cmd.errpos] << endl;
	}

}


int set_arg(const char* param, const char* assign, int &count) {
	double temp_dbl;
	double tmp;
	
	switch(*(param+1)) {	//switch on the first character
		case 'g':
			genome_file = assign;
			break;
		case 'l':
			if(sscanf(assign,"%d",&gSEQ_LENGTH) < 1)
				return PARSE_ERROR;
			break;
		case 'o':
			output_file = assign;
			break;
		case 'a':
			if(sscanf(assign,"%lf",&temp_dbl)< 1)
				return PARSE_ERROR;
			gALIGN_SCORE = (float)temp_dbl;
			break;
		case 'p':
			count--;
			perc = true; // but it's already true.  Just ignore it
			break;
		case 'r':
			count--;
			perc = false;
			break;
		case 'q':
			if(sscanf(assign,"%lf",&temp_dbl) < 1)
				return PARSE_ERROR;
			gCUTOFF_SCORE = (float)temp_dbl;
			break;
		case 'v':
			if(sscanf(assign,"%d",&gVERBOSE) < 1)
				return PARSE_ERROR;
			break;
		case 'c':
			if(sscanf(assign,"%d",&gNUM_THREADS) < 1)
				return PARSE_ERROR;
			break;
		case 'm': 
			if(sscanf(assign,"%d",&gMER_SIZE) < 1)
				return PARSE_ERROR;
			break;
		case 'B':
			if(sscanf(assign,"%d",&gBUFFER_SIZE) < 1)
				return PARSE_ERROR;
			break;
		case 'u':
			count--;
			gUNIQUE = true;
			break;
		case 'T':
			if(sscanf(assign,"%d",&gMAX_MATCHES) < 1)
				return PARSE_ERROR;
			gSKIP_TOO_MANY_REP = false;
			break;
		case 't':
			if(sscanf(assign,"%d",&gMAX_MATCHES) < 1)
				return PARSE_ERROR;
			break;
			gSKIP_TOO_MANY_REP = true;
		case 'h':
			if(sscanf(assign,"%d",&gMAX_HASH_SIZE) < 1)
				return PARSE_ERROR;
			//gSKIP_TOO_MANY_REP = true;
			break;
		case 'S':
			pos_matrix = assign;
			break;
		case 'G':
			//if(sscanf(assign,"%lf",&gGAP) < 1)
			if(sscanf(assign,"%lf",&temp_dbl) < 1)
				return PARSE_ERROR;
			gGAP = (float)temp_dbl;
			break;
		case 'M':
			if(sscanf(assign,"%d",&gMAX_GAP) < 1)
				return PARSE_ERROR;
			break;
		case 'A':
			g_adaptor = new char[strlen(assign)+1];
			unsigned int i;
			for(i=0; i<strlen(assign); i++)
				g_adaptor[i] = tolower((char)assign[i]);
			// Null-terminate the string
			g_adaptor[i] = '\0';
			//g_adaptor = assign;
			break;
		case '0':
			count--;
			gPRINT_FULL = 1;
			break;
		case 'b':
			count--;
			gBISULFITE = 1;
			gGEN_SIZE = 1;
			gPRINT_FULL = 1;
			break;
		case 'd':
			count--;
			gATOG = 1;
			gGEN_SIZE = 1;
			gPRINT_FULL = 1;
			break;
		case 's':
			if(sscanf(assign,"%d",&gGEN_SKIP) < 1)
				return PARSE_ERROR;
			break;
		case 'n':
			if(sscanf(assign,"%lg",&tmp) < 1)
				return PARSE_ERROR;
			gGenSizeBp0 = (unsigned int)tmp;
			break;
			
		//case 'j':
		//	if(sscanf(assign,"%d",&gJUMP_SIZE) < 1)
		//		return PARSE_ERROR;
		//	break;

		//case 'k':
		//	if(sscanf(assign,"%d",&gMIN_JUMP_MATCHES) < 1)
		//		return PARSE_ERROR;
		//	break;

		case '?':
			usage(0,(char*)"");
		default:
			return PARSE_ERROR;
	}

	return 0;

}

enum {
	PAR_ORIG=256,
	PAR_GENOME,
	PAR_LENGTH,
	PAR_OUTPUT,
	PAR_ALIGN_SCORE,
	PAR_PERCENT, // include backward compatability
	PAR_RAW,
	PAR_CUTOFF_SCORE,
	PAR_VERBOSE,
	PAR_NUM_PROC,
	PAR_MER_SIZE,
	PAR_BUFFER,
	PAR_MAX_MATCH,
	PAR_MAX_MATCH2,
	PAR_UNIQUE,
	PAR_MAX_HASH,
	PAR_SUBST_FILE,
	PAR_GAP_PENALTY,
	PAR_MAX_GAP,
	PAR_ADAPTOR,
	PAR_PRINT_FULL,
	PAR_PRINT_ALL_SAM,
	PAR_VCF,
	PAR_BS_SEQ,
	PAR_A_TO_G,
	PAR_FAST,
	PAR_GEN_SKIP,
	PAR_READ,
	PAR_SAVE,
	PAR_GEN_SIZE,
	PAR_JUMP,
	PAR_N_MATCH,
	PAR_SNP,
	PAR_ILL,
	PAR_TOP_HASH,
	PAR_NOQUAL,
	PAR_LARGEMEM,
	PAR_UPSTRAND,
	PAR_DOWNSTRAND,
	PAR_SNP_PVAL,
	PAR_SNP_MONOP,
	PAR_GMP,
	PAR_SW_ALIGN,
	PAR_ASSEMBLER
};

int set_arg_ext(const char* param, const char* assign, int &count) {
	double temp_dbl;
	int which = 0;
	int adjust=0;

	if(strncmp(param+2,"genome=",7) == 0) {	//check to see if it's the genome
		//which = 'g';
		which = PAR_GENOME;
		adjust = 8;	//the size of "genome" plus 2
	}
	else if(strncmp(param+2,"length=",7) == 0) {
		//which = 'l';
		which = PAR_LENGTH;
		adjust = 8;
	}
	else if(strncmp(param+2,"output=",7) == 0) {
		//which = 'o';
		which = PAR_OUTPUT;
		adjust = 8;
	}
	else if(strncmp(param+2,"align_score=",12) == 0) {
		//which = 'a';
		which = PAR_ALIGN_SCORE;
		adjust = 13;
	}
	// only use strcmp when there won't be a second argument
	else if(strcmp(param+2,"percent") == 0) {
		//which = 'p';
		which = PAR_PERCENT;
		adjust = 9;
	}
	else if(strcmp(param+2,"raw") == 0) {
		//which = 'p';
		which = PAR_RAW;
		adjust = 9;
	}
	else if(strncmp(param+2,"read_quality=",13) == 0) {
		which = PAR_CUTOFF_SCORE;
		adjust = 15;
	}
	else if(strncmp(param+2,"verbose=",8) == 0) {
		//which = 'v';
		which = PAR_VERBOSE;
		adjust = 9;
	}
	else if(strncmp(param+2,"num_proc=",9) == 0) {
		//which = 'c';
		which = PAR_NUM_PROC;
		adjust = 10;
	}
	else if(strncmp(param+2,"mer_size=",9) == 0) {
		//which = 'm';
		which = PAR_MER_SIZE;
		adjust = 10;
	}
	else if(strncmp(param+2,"buffer=",7) == 0) {
		//which = 'B';
		which = PAR_BUFFER;
		adjust = 8;
	}
	else if(strncmp(param+2,"max_match=",9) == 0) {
		//which = 'T';
		which = PAR_MAX_MATCH;
		adjust = 10;
	}
	else if(strncmp(param+2,"max_match2=",10) == 0) {
		//which = 't';
		which = PAR_MAX_MATCH2;
		adjust = 11;
	}
	else if(strncmp(param+2,"max_hash=",10) == 0) {
		//which = 'h';
		which = PAR_MAX_HASH;
		adjust = 11;
	}
	else if(strncmp(param+2,"subst_file=",9) == 0) {
		//which = 'S';
		which = PAR_SUBST_FILE;
		adjust = 10;
	}
	else if(strncmp(param+2,"gap_penalty=",12) == 0) {
		//which = 'G';
		which = PAR_GAP_PENALTY;
		adjust = 13;
	}
	else if(strncmp(param+2,"max_gap=",8) == 0) {
		//which = 'M';
		which = PAR_MAX_GAP;
		adjust = 9;
	}
	else if(strncmp(param+2,"adaptor=",8) == 0) {
		//which = 'A';
		which = PAR_ADAPTOR;
		adjust = 9;
	}
	else if(strcmp(param+2,"print_full") == 0) {
		//which = '0';
		which = PAR_PRINT_FULL;
		adjust = 12;
	}
	else if(strcmp(param+2,"print_all_sam") == 0) {
		which = PAR_PRINT_ALL_SAM;
		adjust = 13;
	}
	else if(strcmp(param+2,"vcf") == 0) {
		which = PAR_VCF;
		adjust = 3;
	}
	else if(strcmp(param+2,"a_to_g") == 0) {
		//which = 'd';
		which = PAR_A_TO_G;
		adjust = 8;
	}
	else if(strcmp(param+2,"fast") == 0) {
		//which = 'f';
		which = PAR_FAST;
		adjust = 6;
	}
	else if(strncmp(param+2,"gen_skip=",9) == 0) {
		//which = 's';
		which = PAR_GEN_SKIP;
		adjust = 10;
	}
	else if(strncmp(param+2,"read=",5) == 0) {
		//which = 1;
		which = PAR_READ;
		adjust = 6;
		gREAD=true;
	}
	else if(strncmp(param+2,"save=",5) == 0) {
		//which = 2;
		which = PAR_SAVE;
		adjust = 6;
		gSAVE=true;
	}
	else if(strncmp(param+2,"bin_size=",9) == 0) {
		//which = 3;
		which = PAR_GEN_SIZE;
		adjust = 10;
	}
	else if(strncmp(param+2,"jump=",5) == 0) {
		which = PAR_JUMP;
		adjust = 6;
	}
	else if(strncmp(param+2,"num_seed=", 10) == 0) {
		which = PAR_N_MATCH;
		adjust = 11;
	}
	else if(strcmp(param+2,"snp") == 0) {
		which = PAR_SNP;
		adjust = 5;
	}
	else if(strncmp(param+2,"snp_pval=",9) == 0) {
		which = PAR_SNP_PVAL;
		adjust = 11;
	}
	else if(strcmp(param+2,"snp_monop") == 0) {
		which = PAR_SNP_MONOP;
		adjust = 11;
	}

 	else if(strcmp(param+2,"illumina") == 0) {
		which = PAR_ILL;
		adjust = 10;
	}
	
	else if(strcmp(param+2,"top_hash") == 0) {
		which = PAR_TOP_HASH;
		adjust = 10;
	}	
	else if(strcmp(param+2,"noqual") == 0) {
		which = PAR_NOQUAL;
		adjust = 8;
	}
	else if(strcmp(param+2,"MPI_largemem") == 0) {
		which = PAR_LARGEMEM;
		adjust = 14;
	}
	else if(strcmp(param+2,"up_strand") == 0) {
		which = PAR_UPSTRAND;
		adjust = 11;
	}
	else if(strcmp(param+2,"down_strand") == 0) {
		which = PAR_DOWNSTRAND;
		adjust = 13;
	}
	else if(strcmp(param+2,"gmp") == 0) {
		which = PAR_GMP;
		adjust = 5;
	}
	else if(strcmp(param+2,"sw") == 0) {
		which = PAR_SW_ALIGN;
		adjust = 4;
	}
	else if(strcmp(param+2,"assembler") == 0) {
		which = PAR_ASSEMBLER;
		adjust = 11;
	}
	
	else if(strcmp(param+2,"help") == 0) {
		usage(0,(char*)""); //usage will exit immediately
	}
	else 
		return NO_MATCHING_ARG;

	param += ++adjust;

	switch(which) {	//switch on the first character
		//case 'g':
		case PAR_GENOME:
			genome_file = param;
			break;
		//case 'l':
		case PAR_LENGTH:
			if(sscanf(param,"%d",&gSEQ_LENGTH) < 1)
				return PARSE_ERROR;
			break;
		//case 'o':
		case PAR_OUTPUT:
			output_file = param;
			break;
		//case 'a':
		case PAR_ALIGN_SCORE:
			//if(sscanf(param,"%lf",&ALIGN_SCORE) < 1)
			if(sscanf(param,"%lf",&temp_dbl) < 1)
				return PARSE_ERROR;
			//	return (PARSE_ERROR * (int)(*param));
			gALIGN_SCORE = (float)temp_dbl;
			break;
		//case 'p':
		case PAR_PERCENT:
			//count--;
			perc = true;
			break;
		//case 'r':
		case PAR_RAW:
			//count--;
			perc = false;
			break;
		// case 'q':
		case PAR_CUTOFF_SCORE:
			if(sscanf(param,"%lf",&temp_dbl) < 1)
				return PARSE_ERROR;
			gCUTOFF_SCORE = (float)temp_dbl;
			break;
		//case 'v':
		case PAR_VERBOSE:
			if(sscanf(param,"%d",&gVERBOSE) < 1)
				return PARSE_ERROR;
			break;
		//case 'c':
		case PAR_NUM_PROC:
			if(sscanf(param,"%d",&gNUM_THREADS) < 1)
				return PARSE_ERROR;
			break;
		//case 'm': 
		case PAR_MER_SIZE:
			if(sscanf(param,"%d",&gMER_SIZE) < 1)
				return PARSE_ERROR;
			break;
		//case 'B':
		case PAR_BUFFER:
			if(sscanf(param,"%d",&gBUFFER_SIZE) < 1)
				return PARSE_ERROR;
			break;
		//case 'T':
		case PAR_MAX_MATCH:
			if(sscanf(param,"%d",&gMAX_MATCHES) < 1)
				return PARSE_ERROR;
			gSKIP_TOO_MANY_REP = false;
			break;
		//case 't':
		case PAR_MAX_MATCH2:
			if(sscanf(param,"%d",&gMAX_MATCHES) < 1)
				return PARSE_ERROR;
			gSKIP_TOO_MANY_REP = true;
			break;
		//case 'h':
		case PAR_MAX_HASH:
			if(sscanf(param,"%d",&gMAX_HASH_SIZE) < 1)
				return PARSE_ERROR;
			break;
		//case 'S':
		case PAR_SUBST_FILE:
			pos_matrix = param;
			break;
		//case 'G':
		case PAR_GAP_PENALTY:
			//if(sscanf(param,"%lf",&gGAP) < 1)
			if(sscanf(param,"%lf",&temp_dbl) < 1)
				return PARSE_ERROR;
			gGAP = (float)temp_dbl;
			gGAP_SW = 1.25*gGAP;
			break;
		//case 'M':
		case PAR_MAX_GAP:
			if(sscanf(param,"%d",&gMAX_GAP) < 1)
				return PARSE_ERROR;
			break;
		//case 'A':
		case PAR_ADAPTOR:
			g_adaptor = new char[strlen(assign)];
			for(unsigned int i=0; i<strlen(assign); i++)
				g_adaptor[i] = tolower((char)param[i]);
			//g_adaptor = (char*)param;
			break;
		//case '0':
		case PAR_PRINT_FULL:
			//count--;	//don't decrement in the extended version
			gPRINT_FULL = 1;
			break;
		case PAR_PRINT_ALL_SAM:
			gPRINT_ALL_SAM = true;
			break;
		case PAR_VCF:
			gPRINT_VCF = true;
			break;
		//case 'f':
		case PAR_FAST:
			//count--;
			gFAST = true;
			break;
		//case 's':
		case PAR_GEN_SKIP:
			if(sscanf(param,"%d",&gGEN_SKIP) < 1)
				return PARSE_ERROR;
			break;
		//case 1:	//read
		case PAR_READ:
			if(strlen(param) == 0)
				return PARSE_ERROR;

			gREAD_FN = new char[strlen(param)+10];
			sprintf(gREAD_FN,"%s",param);

			break;
		//case 2:	//save
		case PAR_SAVE:
			if(strlen(param) == 0)
				return PARSE_ERROR;

			gSAVE_FN = new char[strlen(param)+10];
			sprintf(gSAVE_FN,"%s",param);

			break;
		case PAR_GEN_SIZE:
			if(sscanf(param,"%d",&gGEN_SIZE) < 1)
				return PARSE_ERROR;
			break;
/*
		case PAR_JUMP: 
			if(sscanf(param,"%d",&gJUMP_SIZE) < 1)
				return PARSE_ERROR;
			break;

		case PAR_N_MATCH:
			if(sscanf(param,"%d",&gMIN_JUMP_MATCHES) < 1)
				return PARSE_ERROR;
			break;
*/
		case PAR_SNP_PVAL:
			if(sscanf(param,"%f",&gSNP_PVAL) < 1)
				return PARSE_ERROR;
			break;
		case PAR_SNP_MONOP:
			gSNP_MONOP = 1;
			// let it fall through to PAR_SNP
			// We don't want to force them to specify both flags
		case PAR_SNP:
			gSNP = true;
			gGEN_SIZE = 1;
			gPRINT_FULL = 1;
			break;
		//case 'b':
// 		case PAR_BS_SEQ:
// 			//count--;
// 			gBISULFITE2 = true;
// 			gGEN_SIZE = 1;
// 			gPRINT_FULL = 1;
// 			break;
		//case 'd':
		case PAR_A_TO_G:
			//count--;
			gATOG = true;
			gGEN_SIZE = 1;
			gPRINT_FULL = 1;
			break;
		case PAR_ILL:
			gPHRED_OFFSET = ILLUMINA_PHRED_OFFSET;
			break;
		case PAR_TOP_HASH:
			gTOP_K_HASH = 0;
			break;
		case PAR_NOQUAL:
			gPHRED_OFFSET = NO_FASTQ;
			break;
		case PAR_LARGEMEM:
			gMPI_LARGEMEM = true;
			break;
		// Don't match to the negative strand
		case PAR_UPSTRAND:
			gMATCH_NEG_STRAND = false;
			break;
		case PAR_DOWNSTRAND:
			gMATCH_POS_STRAND = false;
			break;
		case PAR_GMP:
			gSAM2GMP = true;
			break;
		case PAR_SW_ALIGN:
			gSOFT_CLIP = true;
			break;
		case PAR_ASSEMBLER:
			break;
		default:
			return NO_MATCHING_ARG;
	}

	return 0;

}

//bring this from inc/align_seq3_raw.cpp here
unsigned int align_sequence(Genome &gen, seq_map &unique, const Read &search, const string &consensus, double min_align_score, double max_align_score2, double &denominator, double &top_align_score, int &cnt_summit, int strand2ali, int align_mode, int thread_id) {

	//min_align_score: the minimum align score or cutoff to be reported and it must be given by user
	//max_align_score2: this is an alignment score nearly closed to the best alignment score for the read
	//top_align_score: this is a top alignment score we observe so far
	
	bin_seq bs;
	unsigned int searchL = search.length;
	unsigned int searchL2 = 0;
	int searchHalfL = (int)floor((double)searchL/2.0);
	unsigned int i,j,last_hash=searchL-gMER_SIZE+1;
	unsigned long beginningE, beginning, top_beginning=0;
	
	string to_match, to_matchE, readE1, readE2;
	unsigned int i_next, maxHit=0, minHashHitsCutoff;
	string consensus_piece;
	HashLocation* hashes = NULL;
	
	unsigned int swMinLen;
	bool probeE1=true, probeE2=true;
	double nearTopAs = min_align_score;
	double min_align_score_sw;
	double align_score = 0.0;
	
	//int aligned = NOTHING2MAP;
	unsigned int cnt_top_map =0;
	int jumpSize;
	
	//Note that an initial top_align_score must be min_align_score.
	
	// A map of genome positions and the number of times they are referenced
	map<unsigned long, unsigned int> possible_locs;
	
	top_align_score = min_align_score;
	
	float earlyTerminationScore = EARLY_STOP_SC_R * min_align_score;
	
	/*--------------------------
	 * collect all hash hit positions
	---------------------------*/

	jumpSize = (int)floor(gMER_SIZE*0.5);
	//jumpSize = 1;
#ifdef DEBUG	
	if (searchL <= gSEQ_LENGTH) {
		jumpSize = (int)floor((searchL-gMER_SIZE)*(gMER_SIZE*0.5-1.0)/(gSEQ_LENGTH-gMER_SIZE)+1.5);
	}
#endif
	
	for(i=0; i<last_hash; i+=jumpSize) {
		hashes = NULL;

		// If we've run accross a hash location that is clear (possibly because we've
		// cleared it in a previous step of the algorithm), we want to get another hash
		// that's right next to it instead of jumping over everything.
		for(j=0; j+i<last_hash; j++) {
			consensus_piece = consensus.substr(i+j,gMER_SIZE);
#ifdef DEBUG
			fprintf(stderr,"Consensus piece: >%s<\n",consensus_piece.c_str());
#endif

			// Let's move this into the Genome class
			//p_hash = bs.get_hash(consensus_piece);
			//hashes = gen.GetMatches(p_hash.second);
			hashes = gen.GetMatches(consensus_piece);
			
			// p_hash.first would only be zero if there were n's in the sequence.
			// Just step to a location that doesn't (?) have any n's.
			if(!hashes)  {
#ifdef DEBUG
				//if(gVERBOSE > 2)
					cerr << "Invalid hash sequence at step " << j+i << endl;
#endif
				continue;
			}

			if(hashes->size) { // we've found a valid location
				i_next = i+j+1;
				break;
			}
#ifdef DEBUG
			//if(gVERBOSE > 2)
				cerr << "At location " << j+i << ", no valid hashes found." << endl;
#endif
		} // end of for-loop identifying valid hash location

		// Increment i by j so we don't look here again.
		i+=j;
		
		if(!hashes || !hashes->size) {	// There are no valid locations for this specific hash
#ifdef DEBUG
			//if(gVERBOSE > 2)
				cerr << "No valid hashes for this sequence." << endl;
#endif
			continue;	// Go to the next location
		}

#ifdef DEBUG
		cerr << "At location " << i << ", found " << hashes->size << " hashes for this string." << endl;
#endif
		
		for(unsigned int vit = 0; vit<hashes->size; ++vit) {
			//Match the sequence to a sequence that has two characters before and after.
			//string to_match = gen.GetString((*vit)-i-2, search.size()+4);
			
			beginning = (hashes->hash_arr[vit]<=i) ? 0 : (hashes->hash_arr[vit]-i);
			
			// Increment the number of times this occurs
			possible_locs[beginning]++;
			if (possible_locs[beginning]>=maxHit)
				maxHit = possible_locs[beginning];
		}
	}

	minHashHitsCutoff = maxHit - gTOP_K_HASH;
	
	if (i_next<last_hash) {
		//for(i=i_next;i<last_hash;i++) {
		for (i=(last_hash-1);i>i_next;i--) {
			hashes = NULL;
			consensus_piece = consensus.substr(i,gMER_SIZE);
			hashes = gen.GetMatches(consensus_piece);
			if(!hashes || !hashes->size)
				continue;
			
			if(hashes->size) { 
				for(unsigned int vit = 0; vit<hashes->size; ++vit) {
					beginning = (hashes->hash_arr[vit]<=i) ? 0 : (hashes->hash_arr[vit]-i);
					possible_locs[beginning]++;
				
					if (possible_locs[beginning]>=maxHit)
						maxHit = possible_locs[beginning];
				}
				break;
			}
		}
	} // end of for over all the hash positions


	/* ----------------------------------------
	 * now, run an approximate DP to obtain potential matches
	 * ----------------------------------------
	 */
	if (maxHit<2)
		return cnt_top_map;

	bool softClipFirstVisitF = true;
	map<unsigned long, unsigned int>::iterator loc_it;
	map<unsigned long, unsigned int>::iterator loc_it_begin;
	map<unsigned long, unsigned int>::iterator loc_it_end;
	unsigned int sumHit, sumHit1, sumHit2, sumHitMax;
	sumHitMax= minHashHitsCutoff;
	unsigned long int hash_winL = (unsigned long int)floor(0.06*searchL);
	unsigned long int delim;
	
	while (true) {
		
		if (align_mode == NW_ALIGN) {
			loc_it_begin = possible_locs.begin();
			loc_it_end = possible_locs.end();
		}
		//else if (align_mode == SPLICE_JT) {
		//	//todo
		//}
		else if (align_mode == SW_ALIGN) {
			if (top_beginning > 0) {
				loc_it_begin = possible_locs.find(top_beginning);
				loc_it_end = loc_it_begin;
			}
			else {
				min_align_score_sw = max_align_score2 * SW_ALIGN_R_CUTOFF;
				if (min_align_score_sw > swMinLen*gMATCH){
					min_align_score = min_align_score_sw;
				}
				else {
					min_align_score = swMinLen*gMATCH;
				}
				top_align_score = min_align_score;
			}
		}
		// prepare hash window

		//for(loc_it = loc_it_begin; loc_it != loc_it_end; ++loc_it) { //TODO: use some offset

		loc_it = loc_it_begin;
		while (loc_it != loc_it_end) {
			
			//find left and right index fit in the window
			sumHit1=0;
			if (loc_it->first >hash_winL) {
				delim = loc_it->first - hash_winL;
				sumHit1=search_delimiter_iterator(loc_it,SEARCH_LEFT,delim,loc_it_begin);
			}
			
			delim = loc_it->first + hash_winL;
			sumHit2=search_delimiter_iterator(loc_it,SEARCH_RIGHT,delim,loc_it_end);
			sumHit = sumHit1+sumHit2+loc_it->second;
			
			if (cnt_top_map>gMAX_PRE_MATCHES_HALF) {
				++loc_it;
				break;
			}
			
			// Define the number of hash locations that have to be matching at each genomic position
			// before we'll even match it

#ifdef DEBUG
			if(loc_it->second < minHashHitsCutoff)
				++loc_it;
				continue;
#endif
			if (sumHit<(sumHitMax-3)){
				++loc_it;
				continue;
			}
			else if (sumHit>sumHitMax){
				sumHitMax=sumHit;
			}
			
			beginning = loc_it->first;
			//check if this is alreayd computed previously
			to_match = gen.GetString(beginning, searchL);
			
			if(to_match.size() == 0) {	//means it's on a chromosome boundary--not valid sequence
	#ifdef DEBUG
				fprintf(stderr,"On chromosome boundary...skipping\n");
	#endif
				++loc_it;
				continue;
			}

			//okay, now we need to work on this loc_it
			align_score = 0.0;
			
	#ifdef DEBUG_NW
			// Record the number of nw's this thread performs
			num_nw[thread_id]++;
	#endif

	#ifdef DEBUG
					cerr << beginning << "," << loc_it->second << ") " << "matching: " << to_match
						<< "  with  : " << consensus << "\tat pos   " << beginning 
														<< "\tand step " << i << endl;
	#endif

			//approximate alignment (banded NW or SW)
			align_score = bs.get_align_score(search,to_match,align_mode,searchHalfL,earlyTerminationScore);
				
			//-----------------------------------------------
			// the following if-statment looks very heuristic but what we want here is to change min_align_score(i.e., a=0.xx) dynamically in order to focus on top hits as many as possible but speed up
			
			if(align_score >= top_align_score) { 
				
				top_align_score = align_score;
				top_beginning = beginning;
				nearTopAs = top_align_score - (top_align_score - min_align_score)* PRE_TOP_R;
				
				if (top_align_score >= max_align_score2) { //check if this is nearly exact match!
					cnt_top_map+=gMAX_MATCHES;
					cnt_summit++;
					if (gUNIQUE && (cnt_summit > 1))
						return cnt_top_map;
					
//add print_all_sam flag to enable the feature
					earlyTerminationScore = EARLY_STOP_SC_R_AFTER_HIT*top_align_score;

	#ifdef DEBUG
				fprintf(stderr,"top_align_score=%g\n",top_align_score);
	#endif
				}
				else { //still cannot find exact match but we can increase the mapping stringency little higher.
					earlyTerminationScore = EARLY_STOP_SC_R*top_align_score;
					cnt_top_map+=1;
				}
				//-----------------------------------------------
				
		#ifdef DEBUG
				cerr << beginning << "," << top_align_score << "," << to_match << endl;
		#endif
			}

			//now, we obtain align_score
			//see if this can be a potential matching
			if(align_score >= nearTopAs) {

				ScoredSeq* temp;
				
				//check 3 primers at both ends to see if they are in exact match!
				
				if ((align_mode == NW_ALIGN) && (beginning > gMAX_GAP) && ((beginning + searchL + gMAX_GAP) < gGenSizeBp)) {

					probeE1 = str2Lower(consensus.substr(0,gMAX_GAP),to_match.substr(0,gMAX_GAP));
					probeE2 = str2Lower(consensus.substr(searchL-gMAX_GAP,gMAX_GAP),to_match.substr(searchL-gMAX_GAP,gMAX_GAP));
					
					
					if (!probeE1 && !probeE2) { // m , m
						to_matchE = to_match;
						beginningE = beginning;
					}
					else if (!probeE1 && probeE2) {// m, m'
						//check if it is extendable
						beginningE = beginning;
						searchL2 = searchL+gMAX_GAP;
						to_matchE = gen.GetString(beginningE, searchL2);
						if (to_matchE.size() ==0) {
							to_matchE = to_match;
							probeE2=false;
						}
					}
					else if (probeE1 && !probeE2) {// m', m
						beginningE = beginning-gMAX_GAP;
						searchL2 = searchL+gMAX_GAP;
						to_matchE = gen.GetString(beginningE, searchL2);
						if(to_matchE.size() == 0) {	//means it's on a chromosome boundary--not valid sequence
								to_matchE = to_match;
								beginningE = beginning;
								probeE1=false;
						}
					}
					else { //m', m'
						beginningE = beginning-gMAX_GAP;
						searchL2 = searchL+2*gMAX_GAP;
						to_matchE = gen.GetString(beginningE, searchL2);
						if(to_matchE.size() == 0) {	//means it's on a chromosome boundary--not valid sequence
							to_matchE = to_match;
							beginningE = beginning;
							probeE1=false;
							probeE2=false;
						}
					}
				}
				else {
					to_matchE = to_match;
					beginningE = beginning;
				}
				
				seq_map::iterator it = unique.find(to_matchE);
				if (it == unique.end()) {
					//note that all to_matchE is from +ive strand in ref genome!
					if(gSNP)
						temp = 
							new SNPScoredSeq(to_matchE, align_score, beginningE, strand2ali, align_mode, probeE1, probeE2);//todo!
					else if(gBISULFITE || gATOG) {
						temp =
							new BSScoredSeq(to_matchE, align_score, beginningE, strand2ali, align_mode, probeE1, probeE2); //we pass to_matchE so that we can refine an exact alignment score later
					}
					else
						temp =
							new NormalScoredSeq(to_matchE, align_score, beginningE, strand2ali, align_mode, probeE1, probeE2);

						unique.insert(pair<string,ScoredSeq*>(to_matchE,temp));
						denominator += exp(align_score);
			#ifdef DEBUG
						fprintf(stderr,"[%10s%c] just added %s at jump %u with size %lu\n",search.name,strand==POS_STRAND ? '+' : '-',gen.GetPos(loc_it->first).c_str(),i,hashes->size);
						fprintf(stderr,"[%10s%c] size of unique is now %lu\n",search.name,strand==POS_STRAND?'+':'-',unique.size());
			#endif
				}
				else {
					if( (*it).second->add_spot(beginningE,strand2ali) ) {
						//fprintf(stderr,"[%s] %lu:%d Not already here\n",search.name,loc_it->first,strand);
						denominator += exp(align_score);
					}
				}
				if (align_mode == SW_ALIGN) { 
					++loc_it;
					break; 
				}
			}//the end of high score hit
			++loc_it;
		} //the end of loop w/ loc_it

		if (gSOFT_CLIP && (cnt_top_map == 0) && softClipFirstVisitF) {
			align_mode = SW_ALIGN;
			softClipFirstVisitF = false;
			swMinLen = (unsigned int)ceil(2*log(EVAL_K*gGenSizeBp0*searchL/gSW_Eval_cutoff)/(gMATCH*gEvalLambda/gADJUST));
			//min_align_score = swMinLen*gMATCH;
	#ifdef DEBUG
			cout << "swMinLen:" << swMinLen << "\n";
	#endif
			if (swMinLen > searchL) {break;}
		}
		else {break;}
		
	} //end of while()

	if (cnt_top_map == 0)
		top_align_score = 0.0;

	return cnt_top_map;
}
