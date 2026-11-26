#include "rank.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <zlib.h>

namespace {
struct Fixture{char directory[128],manifest[256],queries[256],output[256];size_t documents;Fixture():documents(0){strcpy(directory,"/tmp/mai-rank-tests-XXXXXX");if(!mkdtemp(directory)){perror("mkdtemp");abort();}snprintf(manifest,sizeof(manifest),"%s/documents.tsv",directory);snprintf(queries,sizeof(queries),"%s/queries.tsv",directory);snprintf(output,sizeof(output),"%s/results.jsonl",directory);FILE* f=fopen(manifest,"w");if(!f)abort();fputs("id\tpath\turl\ttitle\tjournal\n",f);fclose(f);}~Fixture(){for(size_t i=0;i<documents;i++){char path[256];snprintf(path,sizeof(path),"%s/doc%zu.txt.gz",directory,i);unlink(path);}unlink(manifest);unlink(queries);unlink(output);rmdir(directory);}void add(const char* text){char path[256];snprintf(path,sizeof(path),"%s/doc%zu.txt.gz",directory,documents);gzFile gz=gzopen(path,"wb");if(!gz||gzputs(gz,text)<0||gzclose(gz)!=Z_OK)abort();FILE* f=fopen(manifest,"a");if(!f)abort();fprintf(f,"%zu\t%s\thttps://example.org/%zu\tdoc%zu\ttest\n",documents,path,documents,documents);fclose(f);documents++;}void query(const char* text){FILE* f=fopen(queries,"w");if(!f)abort();fputs(text,f);fclose(f);}int run(const char* mode){fflush(stdout);int saved=dup(STDOUT_FILENO);FILE* file=fopen(output,"w");if(saved<0||!file||dup2(fileno(file),STDOUT_FILENO)<0)abort();char* args[]={const_cast<char*>("rank"),manifest,queries,const_cast<char*>(mode)};int result=ir_rank_main(4,args);fflush(stdout);if(dup2(saved,STDOUT_FILENO)<0)abort();close(saved);fclose(file);return result;}char* read(){FILE* f=fopen(output,"rb");if(!f)abort();fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char* result=(char*)malloc((size_t)n+1);if(!result)abort();if(fread(result,1,(size_t)n,f)!=(size_t)n)abort();result[n]=0;fclose(f);return result;}};
int require(bool value,const char* message){if(value)return 0;fprintf(stderr,"rank test failed: %s\n",message);return 1;}
const char* row(char* data,const char* id){char needle[128];snprintf(needle,sizeof(needle),"\"query_id\":\"%s\"",id);char* p=strstr(data,needle);if(!p)return NULL;char* end=strchr(p,'\n');if(end)*end=0;return p;}
const char* field(const char* record,const char* name){if(!record)return NULL;char needle[128];snprintf(needle,sizeof(needle),"\"%s\":[",name);const char* p=strstr(record,needle);return p?p+strlen(needle):NULL;}
bool array_is(const char* record,const char* name,const char* expected){const char* p=field(record,name);return p&&strncmp(p,expected,strlen(expected))==0&&p[strlen(expected)]==']';}
bool first_score(const char* record,const char* name,double expected){const char* p=field(record,name);if(!p)return false;char* end=NULL;double got=strtod(p,&end);return end!=p&&fabs(got-expected)<1e-12;}
}
int ir_rank_tests(){int failures=0;{
 Fixture f;f.add("alpha beta beta");f.add("alpha alpha");f.add("beta gamma gamma gamma");f.add("zeta");
 f.query("query_id\tquery\nq1\talpha\nq2\talpha beta\nq3\tunknown\nq4\talpha alpha\nq5\tzeta\nq6\t\nq7\tALPHAs\n");failures+=require(f.run("raw")==0,"raw ranking runs");
 char* text=f.read();const char* q1=row(text,"q1");double idf=1+log(5.0/3.0);double expected_tfidf=idf*idf;double expected_bm25=log(2.0)*2.0*2.2/(2.0+1.2*(0.25+0.75*2.0/2.5));
 failures+=require(array_is(q1,"boolean","0,1"),"Boolean AND IDs deterministic");failures+=require(array_is(q1,"tfidf","1,0"),"TF-IDF favors higher normalized term frequency");failures+=require(array_is(q1,"bm25","1,0"),"BM25 ordering");failures+=require(first_score(q1,"tfidf_scores",expected_tfidf),"TF-IDF score equals hand calculation with corpus DF");failures+=require(first_score(q1,"bm25_scores",expected_bm25),"BM25 score equals hand calculation with corpus average length");free(text);
 text=f.read();const char* q2=row(text,"q2");failures+=require(array_is(q2,"boolean","0"),"conjunction requires both terms");free(text);
 text=f.read();const char* q3=row(text,"q3");failures+=require(array_is(q3,"boolean","")&&array_is(q3,"tfidf","")&&array_is(q3,"bm25",""),"unknown query returns empty results");free(text);
 text=f.read();const char* q4=row(text,"q4");failures+=require(array_is(q4,"boolean","0,1"),"duplicate query term does not alter Boolean AND");failures+=require(first_score(q4,"tfidf_scores",2*expected_tfidf),"query frequency contributes to TF-IDF");failures+=require(first_score(q4,"bm25_scores",2*expected_bm25),"query frequency contributes to BM25");free(text);
 text=f.read();const char* q5=row(text,"q5");failures+=require(array_is(q5,"boolean","3"),"all documents compete, including final document");failures+=require(q5&&strstr(q5,"\"corpus_documents\":4"),"full corpus document count recorded");free(text);
 text=f.read();const char* q6=row(text,"q6");failures+=require(array_is(q6,"boolean","")&&array_is(q6,"tfidf","")&&array_is(q6,"bm25",""),"empty query returns empty results");free(text);
 failures+=require(f.run("stem")==0,"stem ranking runs");text=f.read();const char* q7=row(text,"q7");failures+=require(array_is(q7,"tfidf","1,0"),"normalization applies to both query and corpus");free(text);
 }
 {
 Fixture f;for(int i=0;i<8;i++)f.add("tie");f.query("q\ttie\n");failures+=require(f.run("raw")==0,"top five fixture runs");char* text=f.read();const char* q=row(text,"q");failures+=require(array_is(q,"boolean","0,1,2,3,4")&&array_is(q,"tfidf","0,1,2,3,4")&&array_is(q,"bm25","0,1,2,3,4"),"all rankers truncate to five and break score ties by ID");free(text);
 }
 {
 Fixture f;f.add("child person have");f.add("children people has");f.query("q\tchildren has\n");failures+=require(f.run("lemma")==0,"lemma ranking runs");char* text=f.read();const char* q=row(text,"q");failures+=require(array_is(q,"boolean","0,1"),"lemma handles irregular and expanding forms across corpus");free(text);
 }
 if(!failures)fprintf(stderr,"native rank tests: passed\n");return failures?1:0;
}
