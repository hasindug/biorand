#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include <execinfo.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h> 
#include <sys/resource.h>
#include <sys/time.h>

#include "biorand.h"
#include "misc.h"

#include <string>
#include <iostream>
using namespace std;

#define NUM_MAPPINGS 200 //dont chnage this. only supports primary at the moment
#define F_UNMAPPED 0x04
#define F_SECONDARY 0x100
#define F_SUPPLEMENTARY 0x800

#define OVERLAP_BASED_EVAL 1
#define CONSIDER_SUPPLEMENTARY 1
#define CONSIDER_SECONDARY 1 //works only if supplemetary are considered
#define DEBUG_LOOP 1

typedef struct{
    string rid;
    //int32_t query_start;
    //int32_t query_end;
    //int8_t strand;
    string tid;
    int32_t target_start;
    int32_t target_end;
    int32_t mapq;
    int32_t score;
    int32_t flag;
    char strand; //for convenience

    //string qual;

}alignment_t;

typedef struct{
    int32_t num_entries=0; //number of sam entries
	int32_t mapped_reads=0; //total number of reads that map (primary)
	int32_t mappings=0; //total number of alignments 
    int32_t unmapped=0;
    int32_t secondary=0;
    int32_t supplementary=0;

    alignment_t mapping_of_reads[NUM_MAPPINGS];
    int32_t mapping_of_reads_n=0;

} sam_stat_t;


typedef struct{

    int32_t unmapped_in_both=0;
    int32_t same=0;
    int32_t mismatches=0;
    int32_t only_in_a=0;
    int32_t only_in_b=0;
    int32_t pri_a_to_supp_b=0;
    int32_t pri_a_to_sec_b=0;

    FILE* f_only_in_a;
    FILE* f_only_in_b;
    FILE* f_mismatches;

} compare_stat_t;

void insert_alignment(sam_stat_t *read, alignment_t *sam_entry){

#ifndef CONSIDER_SUPPLEMENTARY
    if((sam_entry->flag & (F_UNMAPPED|F_SECONDARY|F_SUPPLEMENTARY))==0){ 
#else
    #ifndef CONSIDER_SECONDARY
        if((sam_entry->flag & (F_UNMAPPED|F_SECONDARY))==0){ 
    #else
        if((sam_entry->flag & (F_UNMAPPED))==0){
    #endif
#endif
        if(read->mapping_of_reads_n>=NUM_MAPPINGS){
            fprintf(stderr,"Too many mapping for read %s\n",sam_entry->rid.c_str());
            fprintf(stderr,"Flag %d\n",sam_entry->flag);
            read->mapping_of_reads_n--;
        }           
        read->mapping_of_reads[read->mapping_of_reads_n]=*sam_entry;
        read->mapping_of_reads_n++;

    }   

}

#ifdef OVERLAP_BASED_EVAL 
//from https://www.biostars.org/p/17891/
static inline int32_t cigar_to_aln(char *cigar,int32_t pos){
    char *c = cigar;
    int32_t pos_end=pos;
    // int32_t n_gap = 0, mlen = 0;
    while(*c!=0){
        char* p2;
        if(!isdigit(*c)){
            fprintf(stderr,"bad cigar string1: %s\n",cigar);
            exit(EXIT_FAILURE);
        }
        int32_t len=strtol(c,&p2,10);
        if(len<=0){
            fprintf(stderr,"bad cigar string2: %s\n",cigar);
            exit(EXIT_FAILURE);
        }
        switch(*p2){
            case 'M':
            {
                pos_end += len; 
                // mlen += len;
                break;
            }
            case 'D':
            {
                // n_gap += len;
                pos_end += len;
                break;
            }
            // case 'I':
            // {
            //    n_gap += len; 
            // }

            default:
            {
                break;
            }
        }
        c=p2+1;
    }
    return pos_end;
}
#endif

void parse_sam_entry(alignment_t *sam_entry, char *buffer){
        //read name
        char *pch = strtok (buffer,"\t\r\n"); assert(pch!=NULL);
        sam_entry->rid = pch;

        //flag
        pch = strtok (NULL,"\t\r\n"); assert(pch!=NULL);
        sam_entry->flag = atoi(pch);

        if(!(sam_entry->flag & 0x04)){        
            sam_entry->strand = (sam_entry->flag&16)? '-' : '+';

            //RNAME
            pch = strtok (NULL,"\t\r\n"); assert(pch!=NULL);
            sam_entry->tid = pch;
            
            //POS
            pch = strtok (NULL,"\t\r\n"); assert(pch!=NULL);
            sam_entry->target_start = atoi(pch);
            
            //MAPQ
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            sam_entry->mapq = atoi(pch);
            
            //CIGAR
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
        #ifdef OVERLAP_BASED_EVAL
            sam_entry->target_end = cigar_to_aln(pch,sam_entry->target_start);
        #endif
            
            //RNEXT
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            
            //PNEXT
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            
            //TLEN
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            
            //SEQ
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            
            //QUAL
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
        
        
            //NM
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            
            //ms
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            
            //alignment score
            pch = strtok (NULL,"\t\r\n");  assert(pch!=NULL);
            int32_t as;
            int32_t ret=sscanf(pch,"AS:i:%d",&as);
            assert(ret>0);
            sam_entry->score=as;
        }
}

static inline int8_t is_correct_exact(alignment_t a, alignment_t b){
    if(a.tid==b.tid && a.target_start==b.target_start && a.strand==b.strand){
        return 1;
    }
    else{
        return 0;
    }

}

#ifdef OVERLAP_BASED_EVAL 
//adapted from paftools
#define ovlp_ratio 0.1
int8_t is_correct_overlap(alignment_t a, alignment_t b)
{
    if (a.tid != b.tid || a.strand != b.strand) return 0;
    float o, l;
    if (a.target_start < b.target_start) {
        if (a.target_end <= b.target_start) return 0;
        o = (a.target_end < b.target_end? a.target_end : b.target_end) - b.target_start;
        l = (a.target_end > b.target_end? a.target_end : b.target_end) - a.target_start;
    } else {
        if (b.target_end <= a.target_start) return 0;
        o = (a.target_end < b.target_end? a.target_end : b.target_end) - a.target_start;
        l = (a.target_end > b.target_end? a.target_end : b.target_end) - b.target_start;
    }
    return o/l > ovlp_ratio? 1 : 0;
}
#endif

void compare_alnread(compare_stat_t *compare, sam_stat_t *reada,sam_stat_t *readb){
    if(reada->mapping_of_reads_n==0 && readb->mapping_of_reads_n==0){
        compare->unmapped_in_both++;
        return;
    }
    //only in b
    if(readb->mapping_of_reads_n>0 && reada->mapping_of_reads_n==0){
        compare->only_in_b++;    
        alignment_t b = readb->mapping_of_reads[0];
        fprintf(compare->f_only_in_b,"%s\t%s\t%d\t%d\t%d\n",b.rid.c_str(),
        b.tid.c_str(),b.target_start,b.mapq, b.score);
        return;
    }
    //only in a
    if(reada->mapping_of_reads_n>0 && readb->mapping_of_reads_n==0){
        compare->only_in_a++;
        alignment_t a = reada->mapping_of_reads[0];
        fprintf(compare->f_only_in_a,"%s\t%s\t%d\t%d\t%d\n",a.rid.c_str(),
        a.tid.c_str(),a.target_start,a.mapq, a.score);
        return;
    }

    int32_t flag=0;
    for(int32_t i=0;i < reada->mapping_of_reads_n ;i++){
        for(int32_t j=0; j < readb->mapping_of_reads_n ;j++){
                alignment_t a = reada->mapping_of_reads[i];
                alignment_t b = readb->mapping_of_reads[j];
                if(a.rid!=b.rid){
                    cout << a.rid << '\t' << b.rid <<endl;
                }
                assert(a.rid==b.rid);
        #ifndef CONSIDER_SUPPLEMENTARY        
            #ifndef OVERLAP_BASED_EVAL 
                //same mapping
                if(is_correct_exact(a,b)){
                    flag++; 
                }
            #else
                if(is_correct_overlap(a,b)){
                    flag++; 
                }
            #endif
        #else
                //in a only take primaries
                if((a.flag&(F_SUPPLEMENTARY|F_SECONDARY))==0){

                    #ifndef OVERLAP_BASED_EVAL 
                        //same mapping
                        if(is_correct_exact(a,b)){
                            flag++;
                            if(b.flag&F_SECONDARY){
                                compare->pri_a_to_sec_b++;
                            }
                            else if(b.flag&F_SUPPLEMENTARY){
                                compare->pri_a_to_supp_b++;
                            }
                            break;
                        }
                    #else
                        if(is_correct_overlap(a,b)){
                            flag++; 
                            if(b.flag&F_SECONDARY){
                                compare->pri_a_to_sec_b++;
                            }
                            else if(b.flag&F_SUPPLEMENTARY){
                                compare->pri_a_to_supp_b++;
                            }
                            break;
                        }
                    #endif


                }
        #endif    
        }
    }  

    assert(flag==0 || flag ==1);
    if(flag){
        compare->same++;
    }
    else{
        compare->mismatches++;
        alignment_t a = reada->mapping_of_reads[0];
        alignment_t b = readb->mapping_of_reads[0];
        fprintf(compare->f_mismatches,"%s\t%s\t%d\t%d\t%d\t%s\t%d\t%d\t%d\n",a.rid.c_str(),
        a.tid.c_str(),a.target_start,a.mapq, a.score,b.tid.c_str(),b.target_start,b.mapq, b.score);
    }

}

void print_sam_stat(char * filename, sam_stat_t* a){
    printf("File %s\n"
    "Number of entries\t%d\n"
    "Total number of alignments\t%d\n"
    "Number of mapped reads\t%d\n"
    "Unmapped\t%d\n"
    "Secondary\t%d\n"
    "Supplementary\t%d\n"
    ,filename,a->num_entries,a->mappings,a->mapped_reads,a->unmapped, a->secondary, a->supplementary);
}

void print_compare_stat(compare_stat_t *compare){
    printf("\nComparison between a and b\n"
    "Unmapped in both\t%d\n"
    "Same\t%d\n"
    "Mismatches\t%d\n"
    "Only in A\t%d\n"
    "Only in B\t%d\n"
    ,compare->unmapped_in_both,compare->same, compare->mismatches, compare->only_in_a, compare->only_in_b);
#ifdef CONSIDER_SUPPLEMENTARY
    printf("Primary in a is a supplementary in b\t%d\n",compare->pri_a_to_supp_b);
#endif
#ifdef CONSIDER_SECONDARY
    printf("Primary in a is a supplementary in b\t%d\n",compare->pri_a_to_sec_b);
#endif

}
void update_sam_stat(sam_stat_t* a,alignment_t curr_a){
    a->num_entries++;
    if(!(curr_a.flag & F_UNMAPPED)){
        a->mappings++;
        if(curr_a.flag & F_SECONDARY){
            a->secondary++;
        }
        else if(curr_a.flag & F_SUPPLEMENTARY){
            a->supplementary++;
        }
        else{
            a->mapped_reads++;
        }
    }
    else{
        a->unmapped++;
    }
            
}

void comparesam(int argc, char* argv[]){
	
    optind=2;

    if (argc-optind < 2) {
        fprintf(
            stderr,
            "Usage: %s %s [OPTIONS] a.sam b.sam\n",
            argv[0],argv[1]);
        exit(EXIT_FAILURE);
    }

    FILE* samfile_a= fopen(argv[optind],"r");
    F_CHK(samfile_a,argv[optind]);
    FILE* samfile_b= fopen(argv[optind+1],"r");
    F_CHK(samfile_b,argv[optind+1]);


    /****************************sam A**********************************/
    //buffers for getline
    size_t bufferSize_a = 4096;
    char *buffer_a = (char *)malloc(sizeof(char)*(bufferSize_a)); 
    MALLOC_CHK(buffer_a);
    int readlinebytes_a=1;

#ifdef DEBUG_LOOP
    int32_t num_entries_a=0;
	int32_t mapped_reads_a=0;
	int32_t mappings_a=0;
#endif 

    alignment_t prev_a;
    prev_a.rid = "";
    alignment_t curr_a;
    curr_a.rid = "";

    sam_stat_t a;

    /****************************sam b********************************/
    //buffers for getline
    size_t bufferSize_b = 4096;
    char *buffer_b = (char *)malloc(sizeof(char)*(bufferSize_b)); 
    MALLOC_CHK(buffer_b);
    int readlinebytes_b=1;
    //char *pch_b=NULL;

#ifdef DEBUG_LOOP
    int32_t num_entries_b=0;
    int32_t mapped_reads_b=0;
    int32_t mappings_b=0;
#endif

    alignment_t prev_b;
    prev_b.rid = "";
    alignment_t curr_b;
    curr_b.rid = "";

    sam_stat_t b;

    compare_stat_t compare;
    compare.f_only_in_a=fopen("only_in_a.tsv","w");
    fprintf(compare.f_only_in_a,"readID\ttarget\ttarget_pos\tmapq\tAS\n");
    compare.f_only_in_b=fopen("only_in_b.tsv","w");
    fprintf(compare.f_only_in_b,"readID\ttarget\ttarget_pos\tmapq\tAS\n");
    compare.f_mismatches=fopen("mismatches.tsv","w");
    fprintf(compare.f_mismatches,"readID\ttarget_a\ttarget_pos_a\tmapq_a\tAS_a\ttarget_b\ttarget_pos_b\tmapq_b\tAS_b\n");

    int8_t state=0;

    /** Read SAM A*/
	while(1){
        readlinebytes_a=getline(&buffer_a, &bufferSize_a, samfile_a); 
        if(readlinebytes_a == -1){
            assert(getline(&buffer_b, &bufferSize_b, samfile_b)==-1);
            if(state==1){
                compare_alnread(&compare,&a,&b);  
            }
            break;
        } 

        //ignore header lines
        if(buffer_a[0]=='@' || buffer_a[0]=='\n' || buffer_a[0]=='\r'){
            continue;
        }
        
        parse_sam_entry(&curr_a, buffer_a);
        update_sam_stat(&a,curr_a);

    #ifdef DEBUG_LOOP
        if(!(curr_a.flag & 0x04)){
            mappings_a++;
        }
        num_entries_a++;
    #endif

        
        if(curr_a.rid==prev_a.rid ){
            //cout <<  "enter a " << curr_a.rid<< endl;
            insert_alignment(&a, &curr_a);  
        }

        else{
            
        #ifdef DEBUG_LOOP
            if(!(curr_a.flag & 0x04)){
                mapped_reads_a++;
            }
        #endif

            int8_t break_flag = 0;

            /** Now read the entries for the readID for SAMA, from SAM B */   
            while(1){
                readlinebytes_b=getline(&buffer_b, &bufferSize_b, samfile_b); 
                if(readlinebytes_b == -1){
                    break_flag=-1;
                    break;
                } 

                //ignore header lines
                if(buffer_b[0]=='@' || buffer_b[0]=='\n' || buffer_b[0]=='\r'){
                    continue;
                }
                
                parse_sam_entry(&curr_b, buffer_b);
                update_sam_stat(&b,curr_b);
            #ifdef DEBUG_LOOP    
                if(!(curr_b.flag & 0x04)){
                    mappings_b++;
                }    
                num_entries_b++;
            #endif

                if(curr_b.rid==prev_b.rid ){
                    //cout <<  "enter b " << curr_b.rid<< endl;
                    insert_alignment(&b, &curr_b);
                    
                }
                else{
                    prev_b = curr_b;
                    break_flag=1;
                    break;
                }
                
                prev_b = curr_b;

            }

            //do the comparson for the mappings in the read    
            if(state==1){
                compare_alnread(&compare,&a,&b);  
                state=0;
            }
        

            a.mapping_of_reads_n=0;
            //cout <<  "enter a2 " << curr_a.rid<< endl;
            insert_alignment(&a,&curr_a);
            state=1;
           

            assert(break_flag!=0);
            if(break_flag==1){
            #ifdef DEBUG_LOOP      
                 if(!(curr_b.flag & 0x04)){
                     mapped_reads_b++;
                 }
            #endif
                b.mapping_of_reads_n=0;
                //cout <<  "enter  b2 " << curr_b.rid << endl;

                insert_alignment(&b,&curr_b);
            }

        }
        prev_a = curr_a;
    }

#ifdef DEBUG_LOOP
    fprintf(stderr,"[%s] Num entries in a %d, mappings_a %d, mapped reads in a %d, Num_entries in b %d, mappings_b %d, mapped reads in b %d\n",
            __func__, num_entries_a, mappings_a,mapped_reads_a,num_entries_b, mappings_b,mapped_reads_b);

    assert(a.num_entries==num_entries_a);
    assert(b.num_entries==num_entries_b);
    assert(a.mappings==mappings_a);
    assert(b.mappings==mappings_b);
    assert(a.mapped_reads==mapped_reads_a);
    assert(b.mapped_reads==mapped_reads_b);
#endif

    assert(a.num_entries == a.unmapped + a.mapped_reads + a.secondary + a.supplementary);
    assert(a.mappings==a.mapped_reads + a.secondary + a.supplementary);
    assert(b.num_entries == b.unmapped + b.mapped_reads + b.secondary + b.supplementary);
    assert(b.mappings==b.mapped_reads + b.secondary + b.supplementary);

    assert(compare.unmapped_in_both+compare.only_in_a==b.unmapped);
    assert(compare.unmapped_in_both+compare.only_in_b==a.unmapped);
    assert(compare.same+compare.mismatches+compare.only_in_a==a.mapped_reads);
    assert(compare.same+compare.mismatches+compare.only_in_b==b.mapped_reads);
    

    print_sam_stat(argv[optind],&a);
    print_sam_stat(argv[optind+1],&b);
    print_compare_stat(&compare);

    fclose(compare.f_mismatches);
    fclose(compare.f_only_in_a);
    fclose(compare.f_only_in_b);
    fclose(samfile_a);
    fclose(samfile_b);
    free(buffer_a);
    free(buffer_b);
}
