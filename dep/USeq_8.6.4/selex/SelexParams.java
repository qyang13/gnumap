package selex;
import java.io.*;
import java.util.*;
import java.util.regex.*;

import util.gen.*;

/**Hold parameters and results generated by the {@link SelexSeqParser}; also generates reports. */
public class SelexParams {
    //param defaults, modify accordingly
    String restSite = "GGATCC";
    int scoreCutOff = 20;
    int lenMin = 15;
    int lenMax = 17;
    int expectedSize = 16;
    
    //for fixing a filled in BamHI site that is then ligated into a SmaI site
    private String leftSideMatch = "CCCCgatcc";
    private String leftSideReplace = "CCCCggatcc";
    private String rightSideMatch = "ggatcGGGT";
    private String rightSideReplace = "ggatccGGGT";
    private boolean modifyEnds = true;
	public String getLeftSideMatch() {
		return leftSideMatch;
	}
	public String getLeftSideReplace() {
		return leftSideReplace;
	}
	public boolean modifyEnds() {
		return modifyEnds;
	}
	public String getRightSideMatch() {
		return rightSideMatch;
	}
	public String getRightSideReplace() {
		return rightSideReplace;
	}
    
    
    //fields to be set each run
    String[] seqFiles;
    String[] qualFiles;
    String subSeqFastaFile;
    String screenDumpFile;
    String pathForDataFiles;
    String nameForDataFiles;
    
    //totals for summary report
    StringBuffer running = new StringBuffer();  
    ArrayList subSeqs = new ArrayList(500);
    ArrayList subLengths = new ArrayList(500);
    int numFiles =0;
    int numSeqReads =0;
    int numSubs =0;
    int numSubsScores =0;
    int numSubsScoresLength=0;
    int numBadSeqReads = 0;
    int numNoInsertSeqReads = 0;
    int numShortInsertSeqReads =0;
    ArrayList expectedSubSeqs = new ArrayList();//list of Integer objects
    ArrayList observedSubSeqs = new ArrayList(); //list of Integer objects
    ArrayList obsExpRatios = new ArrayList();	//list of Double objects
    int aveNumExpSubSeqs =0;
    
    //getter methods
    public String getRestSite(){return restSite;}
    public String getSeqFile(int index){return seqFiles[index];}
    public String[] getSeqFiles(){return seqFiles;}
    public String getQualFile(int index){return qualFiles[index];}
    public String[] getQualFiles(){return qualFiles;}
    public int getScoreCutOff(){return scoreCutOff;}
    public int getLenMin(){return lenMin;}
    public int getLenMax(){return lenMax;}
    public int getExpectedSize(){return expectedSize;}
    public int getAveNumExpSubSeqs(){
    	return aveNumExpSubSeqs = UtilSelex.averageIntegerArrayList(expectedSubSeqs);
    }
    public int getAveNumObsSubSeqs(){
    	return UtilSelex.averageIntegerArrayList(observedSubSeqs);
    }
    public int getAveExpSeqLength(){
    	if (aveNumExpSubSeqs == 0) getAveNumExpSubSeqs();
    	return aveNumExpSubSeqs*(expectedSize+restSite.length());
    }
    public String getAveObsExpRatio(){
    	return UtilSelex.averageDoubleArrayList(obsExpRatios);
    }
    public StringBuffer getRunning(){return running;}
    
    //primary methods
    public void addObsExpRatio(double ratio){
		obsExpRatios.add(new Double(ratio));
    }
	public void addExpectedSubSeq(int expects){
		expectedSubSeqs.add(new Integer(expects));
	}
	public void addObservedSubSeq(int obs){
		observedSubSeqs.add(new Integer(obs));
	}
    
    public void incrementNumShortInsertSeqReads(){
		numShortInsertSeqReads++;
    }
	public void incrementNumNoInsertSeqReads(){
		numNoInsertSeqReads++;
	}
	public void incrementNumBadSeqReads(){
		numBadSeqReads++;
	}
	public void processArgs(String[] args){
		/*this method will process each argument and assign any new varibles*/
		if (args.length == 0) {
			UtilSelex.printDoc();
			System.exit(0);
		}
		boolean path = false;
		Pattern pat = Pattern.compile("-[a-z]");
		for (int i = 0; i<args.length; i++){
			String lcArg = args[i].toLowerCase();
			Matcher mat = pat.matcher(lcArg);
			if (mat.matches()){
				char test = args[i].charAt(1);
				try{
					switch (test){
						case 's': seqFiles = args[i+1].split(","); i++; break;
						case 'q': qualFiles = args[i+1].split(","); i++; break;
						case 'c': scoreCutOff = Integer.parseInt(args[i+1]); i++; break;
						case 'i': lenMin =Integer.parseInt(args[i+1]); i++; break;
						case 'm': restSite = args[i+1].toUpperCase(); i++; break;
						case 'a': lenMax =Integer.parseInt(args[i+1]); i++; break;
						case 'e': expectedSize =Integer.parseInt(args[i+1]); i++; break;
						case 'p': pathForDataFiles = args[i+1]; i++; path=true; break;
						case 'n': nameForDataFiles = args[i+1]; i++; break;
						case 'H':
						case 'h': UtilSelex.printDoc(); System.exit(0);
						default: System.out.println("Sorry, I don't recognize this parameter option! " + mat.group());
					}
				}
				catch (Exception e){
					System.out.print("Sorry, something doesn't look right with this parameter request: -"+test);
					System.out.println();
					//e.printStackTrace();
					System.exit(0);
				}
			}
		}
		//check to see if they entered paired s and q
		if (seqFiles==null || qualFiles==null){
			System.out.println("\n\nDid you enter one quality file for each sequence file?  I'm not seeing pairs.\n\n");
			System.exit(0);
		}
			if (seqFiles.length != qualFiles.length){ System.out.println("\n\nDid you enter one quality file for each sequence file?  I'm not seeing pairs.\n\n");
			System.exit(0);
		}
		//check to see if files/directories exist
		if (path) IO.checkFile(pathForDataFiles);
		for (int i=0; i<seqFiles.length; i++){
			IO.checkFile(seqFiles[i]);
			IO.checkFile(qualFiles[i]);
		}
		//build names for data files
		try{
			File f= new File(seqFiles[0]);
			String name = f.getName();
			String pathName = f.getCanonicalPath();
			String rPath = pathName.substring(0, pathName.length() - name.length());
            
			if (pathForDataFiles == null){
				pathForDataFiles = rPath;
			}
			if (nameForDataFiles == null){
				nameForDataFiles = name;
			}
			subSeqFastaFile = pathForDataFiles+nameForDataFiles+".SSPFasta";
			screenDumpFile = pathForDataFiles+nameForDataFiles+".SSPResults";
		}
		catch (IOException e) {
			e.printStackTrace();
		}
	}    
    public void printSave(String newString){
        running.append(newString);
        System.out.print(newString);
    }
    public void appendSub(String sub){subSeqs.add(sub);}
    public void incNumSubs(int x, int y, int z){
        numSubs+=x;
        numSubsScores+=y;
        numSubsScoresLength+=z;
    }
    public void appendLength(int x){subLengths.add(new Integer(x));}
    public void incNumFiles(int x){numFiles+=x;}
    public void incNumSeqReads(int x){numSeqReads+=x;}
    public String makeFinalReport(){
        return
        "\n**********************************************************************************************************\n\n"+
        "Selex Sequence Parser Summary Report:\n"+
        "\nInput Files:\n"+
        "  Sequence files: "+Misc.stringArrayToString(seqFiles, " ")+
        "\n  Quality files: "+Misc.stringArrayToString(qualFiles, " ")+
        "\n\nParameters:\n" +
        "  Motif used to parse the sequences: " +restSite+
        "\n  Minimal base quality score: "+ scoreCutOff+
        "\n  Minimal oligo length: "+lenMin+" (not including the flanking motifs)"+
        "\n  Maximal oligo length: "+lenMax+" (not including the flanking motifs)"+
		"\n  Expected oligo size: "+expectedSize+" (not including the flanking motifs)"+
		"\n     (Used to calculate the number of short insert, clone, sequence reads)"+
		"\n\nStatistics:"+
		"\n By File:"+
        "\n  Number of paired files processed: "+numFiles+
        "\n By Sequence Read:"+
        "\n  Number of sequences read: "+numSeqReads+
        "\n  Number of failed sequence reads: "+numBadSeqReads+" ("+(100*numBadSeqReads/numSeqReads)+"%)"+
        "\n     (Reads lacking a continuous stretch of 100 quality scores of "+scoreCutOff+" or better.)"+
        "\n  Number of no insert, good quality sequence reads: "+numNoInsertSeqReads+" ("+(100*numNoInsertSeqReads/(numSeqReads-numBadSeqReads))+"%)"+
        "\n     (Reads with good quality yet less than 2 "+restSite+ " motifs.)"+
        "\n  Number of short insert, qood quality sequence reads: "+numShortInsertSeqReads+" ("+(100*numShortInsertSeqReads/(numSeqReads-numBadSeqReads))+"%)"+
        "\n     (Reads with less than 70% the expected number of sub sequences.)"+
        "\n By Clone: (For those clones with one or more expected sub sequences.)"+
        "\n  Average number of expected sub sequences : "+getAveNumExpSubSeqs()+
        "\n  Average effective sequence length : "+ getAveExpSeqLength()+
        "\n     (Average number expected sub sequences x (expected oligo size + motif length).)"+
        "\n  Average observed number of sub sequences passing quality and length filters: "+getAveNumObsSubSeqs()+
        "\n  Average O/E ratio (# of observed sub seqs vs # of expected sub seqs.): "+getAveObsExpRatio()+
        "\n\nSub Sequence Totals:"+
        "\n  Number of raw sub seqs identified: "+numSubs+
        "\n  Number of good quality sub seqs: "+numSubsScores+" ("+(100*numSubsScores/numSubs)+"%) (these are plotted in the histogram)"+
        "\n  Number of quality subs with a proper length: "+numSubsScoresLength+" ("+(100*numSubsScoresLength/numSubsScores)+"%)"+" (these are written to the Sub Seq Fasta File)"+
        "\n\nOutput Files:\n"+
        "  File containing this screen output: "+screenDumpFile+
        "\n  File containing the Sub Seq Fasta File: "+subSeqFastaFile+
        "\n\n**********************************************************************************************************\n";
    }
    
    public void writeSubSeqFasta(){
        int len = subSeqs.size();
        if (len==0){
            IO.writeString("No Selex oligos passed both the quality and length filters.  Check the .SSPResults file for details.",subSeqFastaFile);
            return;
        }
        StringBuffer sb= new StringBuffer(len);
        //first
        sb.append(">1\n"+(String)subSeqs.get(0));
        for (int i=1; i<len; i++){
            sb.append("\n>"+(i+1)+"\n"+(String)subSeqs.get(i));
        }
        IO.writeString(new String(sb), subSeqFastaFile);
    }
    public void writeReport(){
        IO.writeString(new String(running), screenDumpFile);
    }
    public String plotHistogram(){
        /**This histogram plotter will plot all lengths from 1 base to twice the max sub sequence size
         * this is done to eliminate runaway outliers.*/
        
        int avePlus = (lenMax+restSite.length()) * 2; //estimate of max length to plot
        //create and fill arrays to hold numbers and stars
        int[] tallys = new int[avePlus];
        Arrays.fill(tallys, 0);
        StringBuffer[] stars = new StringBuffer[avePlus];
        for (int i=0; i<avePlus; i++) stars[i]=new StringBuffer("|");
        
        //create array where index represents a length of a sub sequence, the value the number of those sub sequences
        subLengths.trimToSize();
        int len = subLengths.size();
        boolean plotHisto = true;
        for (int i=0; i<len; i++) {
            int x = ((Integer)subLengths.get(i)).intValue();
            if (x<avePlus) {           	
                tallys[x]++;
                stars[x].append("*");
                plotHisto = false;
            }
        }
        //check to see if there is anything to plot
        if (plotHisto) {
            return "\n\n***** No subsequences were found passing the quality and length filters! *****\n\nAre you using the correct restriction fragment motif?\nDid you set generous minimum and maximum length cut offs?\nIs the sequence quality poor?\n\n"; 
        }
        //find zero boundaries in the length array to trim edges
        int start=1;
        int stop=avePlus-1;
        for (int i=1; i<avePlus; i++){
            if (tallys[i]!=0){
                start = i-1;
                if (start<1) start =1;
                break;
            }
        }
        for (int i=stop; i>0; i--){
            if (tallys[i]!=0){
                stop = i+2;
                if (stop>avePlus) stop=avePlus;
                break;
            }
        }
        //print the histogram
        StringBuffer text = new StringBuffer();
        text.append("***************Histogram of Sub Sequence Lengths*****************\nThis is a plot of sub sequences passing the quality score cutoff.\nSub sequences greater than twice the max length are not shown.\n\nLength\t #\tHistogram\n\n");
        for (int i=start; i<stop; i++) text.append("  "+i+"\t("+tallys[i]+")\t\t"+stars[i]+"\n");
        return new String(text);
    }
    

}