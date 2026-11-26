#include "inspection.h"
#include "text.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace web {
namespace {
bool argument(const char *input, const char *name, Text &value) {
    if (!input) return false;
    const char *part=input;
    while (*part) {
        const char *end=strchr(part,'&');if(!end)end=part+strlen(part);
        const char *equal=(const char*)memchr(part,'=',end-part);
        if(equal && (size_t)(equal-part)==strlen(name) && !strncmp(part,name,equal-part))
            return decode_url(equal+1,end-equal-1,value);
        part=*end?end+1:end;
    }
    return false;
}
bool read_prefix(const char *path,Text &text,size_t limit=128*1024) {
    FILE *file=fopen(path,"rb");if(!file)return false;
    char buffer[4096];size_t n;
    while(text.size<limit && (n=fread(buffer,1,sizeof buffer,file)))text.add(buffer,n);
    bool good=!ferror(file);fclose(file);return good;
}
bool number(const char *json,const char *name,double &value) {
    Text key;key.format("\"%s\"",name);const char *start=strstr(json,key.data);
    if(!start)return false;
    start=strchr(start+key.size,':');if(!start)return false;
    char *end=NULL;value=strtod(start+1,&end);
    return end!=start+1 && isfinite(value);
}
void size_text(Text &out,uint64_t bytes) {
    out.format("%llu Б  (%.2f МиБ)",(unsigned long long)bytes,(double)bytes/(1024*1024));
}
bool file_size(const char *path,uint64_t &bytes) {
    struct stat info;
    if(stat(path,&info)||!S_ISREG(info.st_mode))return false;
    bytes=(uint64_t)info.st_size;return true;
}
bool saved_number(Text &out,const char *json,const char *key,const char *label,int digits=0) {
    double value;
    if(!number(json,key,value)){out.format("%s: нет данных\n",label);return false;}
    out.format("%-30s %.*f\n",label,digits,value);return true;
}
int index_sizes(Text &out) {
    out.add("ФАЙЛЫ ГОТОВОГО ИНДЕКСА\nРазмеры измерены сейчас, файлы не изменяются.\n\n");
    const char *names[]={"dictionary.bin","postings.bin","documents.bin","meta.bin","stats.json"};
    uint64_t total=0;
    for(const char *name:names){
        Text path;path.format("lab01_corpus/data/native_index/%s",name);uint64_t bytes;
        if(!file_size(path.data,bytes)){out.format("Не найден файл: %s\n",path.data);return 500;}
        out.format("%-19s ",name);size_text(out,bytes);out.add("\n");total+=bytes;
    }
    out.add("\nИтого: ");size_text(out,total);
    out.add("\n\nСловарь хранит термины и смещения; постинги — ID документов.\nПрямой каталог содержит заголовки и ссылки.\n");
    uint64_t archive;
    if(file_size("lab07_index/results/native_index.tar.gz",archive)){out.add("\nСжатый архив: ");size_text(out,archive);out.add("\n");}
    return 200;
}
int index_counts(Text &out) {
    Text stats;if(!read_prefix("lab01_corpus/data/native_index/stats.json",stats))return 500;
    out.add("ПАРАМЕТРЫ ПОЛНОГО ИНДЕКСА\nСохранённые измерения последней полной сборки.\n\n");
    bool good=true;
    good &=saved_number(out,stats.data,"documents","Документов:");
    good &=saved_number(out,stats.data,"tokens","Токенов:");
    good &=saved_number(out,stats.data,"terms","Терминов:");
    good &=saved_number(out,stats.data,"postings","Постингов:");
    good &=saved_number(out,stats.data,"blocks","Блоков:");
    good &=saved_number(out,stats.data,"block_token_limit","Лимит токенов блока:");
    good &=saved_number(out,stats.data,"average_term_length","Средняя длина термина:",3);
    good &=saved_number(out,stats.data,"time_seconds","Время полной сборки, с:",3);
    out.add("\nПостинг — один ID статьи в списке термина.\nЧастота повторений внутри статьи не увеличивает число ID.\n");return good?200:500;
}
int corpus_sizes(Text &out) {
    DIR *folder=opendir("lab01_corpus/data/text/scoap3");if(!folder)return 500;
    uint64_t count=0,total=0;dirent *entry;
    while((entry=readdir(folder))){
        size_t length=strlen(entry->d_name);
        if(length<7||strcmp(entry->d_name+length-7,".txt.gz"))continue;
        Text path;path.format("lab01_corpus/data/text/scoap3/%s",entry->d_name);uint64_t bytes;
        if(!file_size(path.data,bytes)){closedir(folder);return 500;}
        count++;total+=bytes;
    }
    closedir(folder);
    out.add("РАЗМЕР КОРПУСА\nКоличество и размер gzip-файлов измерены сейчас.\n\n");
    out.format("Статей на диске: %llu\n",(unsigned long long)count);
    out.add("Сжатые тексты: ");size_text(out,total);out.add("\n");
    Text stats;
    if(!read_prefix("lab01_corpus/results/stats.json",stats))return 500;
    double text_bytes;
    if(!number(stats.data,"text_bytes",text_bytes))return 500;
    out.add("\nСохранённый размер распакованных текстов: ");size_text(out,(uint64_t)text_bytes);
    out.add("\nТексты для этого просмотра не распаковываются.\n");return 200;
}
int memory(Text &out) {
    rusage usage;if(getrusage(RUSAGE_SELF,&usage))return 500;
    uint64_t peak=(uint64_t)usage.ru_maxrss,current=0;
#ifdef __APPLE__
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    if(task_info(mach_task_self(),MACH_TASK_BASIC_INFO,(task_info_t)&info,&count)!=KERN_SUCCESS)return 500;
    current=info.resident_size;
#else
    peak*=1024;
    FILE *file=fopen("/proc/self/status","r");if(!file)return 500;
    char line[256];
    while(fgets(line,sizeof line,file)){unsigned long long kb;if(sscanf(line,"VmRSS: %llu kB",&kb)==1)current=kb*1024;}
    fclose(file);
#endif
    out.add("ПАМЯТЬ ПРИЛОЖЕНИЯ\nТекущее измерение процесса веб-сервера.\n\n");
    out.add("Оперативная память сейчас: ");size_text(out,current);
    out.add("\nМаксимум за время процесса: ");size_text(out,peak);
    out.format("\nPID сервера: %ld\n",(long)getpid());
    out.add("\nЛабораторные выполняются в дочерних процессах.\nИх пиковая память показывается после отдельного запуска.\nРазмер индекса на диске не равен используемой памяти.\n");return 200;
}
int crawler_state(Text &out) {
    Text stats;if(!read_prefix("lab02_crawler/results/raw_crawl_summary.json",stats))return 500;
    out.add("СОХРАНЁННЫЙ ИТОГ ПОЛНОЙ ЗАГРУЗКИ\nЭто отчёт о завершённой загрузке, а не новый сетевой обход.\n\n");
    bool good=true;
    good &=saved_number(out,stats.data,"canonical_articles","Статей:");
    good &=saved_number(out,stats.data,"raw_original_documents","Оригиналов XML/PDF:");
    good &=saved_number(out,stats.data,"raw_html_documents","HTML-страниц:");
    good &=saved_number(out,stats.data,"raw_resource_documents","Всего ресурсов:");
    good &=saved_number(out,stats.data,"pending","Осталось в очереди:");
    good &=saved_number(out,stats.data,"errors","Ошибок:");
    out.add("\nXML/PDF и HTML одной статьи — два ресурса, но одна статья.\n");return good?200:500;
}
int timings(Text &out) {
    Text stats;if(!read_prefix("lab08_search/results/benchmark.json",stats))return 500;
    out.add("ИЗМЕРЕНИЯ ПОИСКА\nСохранённый полный прогон: один прогрев и семь повторов.\nВремя запуска процесса учитывается отдельно.\n\n");
    saved_number(out,stats.data,"startup_including_process_ms","Загрузка с запуском, мс:",3);
    out.add("\nПодробные запросы и результаты:\n");out.add(stats.data);out.add("\n");return 200;
}
int zipf_summary(Text &out) {
    Text stats;if(!read_prefix("lab05_zipf/results/zipf.json",stats))return 500;
    out.add("ЗАКОН ЦИПФА — ПОЛНЫЙ КОРПУС\nСохранённые результаты полного расчёта.\n\n");
    bool good=true;
    good &=saved_number(out,stats.data,"documents","Статей:");
    good &=saved_number(out,stats.data,"total_tokens","Токенов:");
    good &=saved_number(out,stats.data,"unique_terms","Уникальных слов:");
    good &=saved_number(out,stats.data,"hapax_legomena","Слов с частотой 1:");
    good &=saved_number(out,stats.data,"time_seconds","Время расчёта, с:",3);
    out.add("\nГрафик: lab05_zipf/results/zipf.svg\n\nПолные результаты:\n");out.add(stats.data);out.add("\n");
    return good?200:500;
}
void quote(Text &out,const char *text){
    out.letter('"');for(;*text;text++){
        unsigned char c=(unsigned char)*text;
        if(c=='"'||c=='\\'){out.letter('\\');out.letter((char)c);}
        else if(c<32)out.format("\\u%04x",c);
        else out.letter((char)c);
    }out.letter('"');
}
int unit_tests(const char *parameters,Text &body){
    Text value;
    if(!argument(parameters,"lab",value)|| (strcmp(value.data,"3")&&strcmp(value.data,"4"))){body.add("{\"ok\":false,\"output\":\"Выберите тесты лабы 3 или 4\"}");return 400;}
    FILE *capture=tmpfile();if(!capture)return 500;
    fflush(NULL);pid_t child=fork();
    if(child<0){fclose(capture);return 500;}
    if(child==0){
        alarm(10);dup2(fileno(capture),STDOUT_FILENO);dup2(fileno(capture),STDERR_FILENO);
        int result=!strcmp(value.data,"3")?ir_tokenizer_tests():ir_stemmer_tests();
        fflush(stdout);fflush(stderr);_exit(result);
    }
    int result=0;pid_t waited;
    do{waited=waitpid(child,&result,0);}while(waited<0&&errno==EINTR);
    rewind(capture);Text output;char buffer[4096];size_t n;
    while(output.size<256*1024 && (n=fread(buffer,1,sizeof buffer,capture)))output.add(buffer,n);
    fclose(capture);bool ok=waited==child&&WIFEXITED(result)&&WEXITSTATUS(result)==0;
    body.format("{\"ok\":%s,\"output\":",ok?"true":"false");quote(body,output.data);body.add("}");return ok?200:500;
}
}
bool inspection_route(const char *path,const char *parameters,Text &body,const char *&type,int &status){
    if(!strcmp(path,"/api/tests")){type="application/json; charset=utf-8";status=unit_tests(parameters,body);return true;}
    if(strcmp(path,"/api/info"))return false;
    type="text/plain; charset=utf-8";Text kind;
    if(!argument(parameters,"kind",kind)){status=400;body.add("Не указан вид проверки");return true;}
    if(!strcmp(kind.data,"index"))status=index_sizes(body);
    else if(!strcmp(kind.data,"terms"))status=index_counts(body);
    else if(!strcmp(kind.data,"corpus"))status=corpus_sizes(body);
    else if(!strcmp(kind.data,"memory"))status=memory(body);
    else if(!strcmp(kind.data,"crawler"))status=crawler_state(body);
    else if(!strcmp(kind.data,"timing"))status=timings(body);
    else if(!strcmp(kind.data,"zipf"))status=zipf_summary(body);
    else{status=400;body.add("Неизвестный вид проверки");}
    if(status>=500)body.add("\nНе удалось прочитать необходимые файлы.\n");
    return true;
}
int ir_inspection_main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: inspect corpus|index|terms|memory|crawler|timing|zipf\n");return 2;}
    Text query,body;query.format("kind=%s",argv[1]);const char *type="text/plain";int status=200;
    inspection_route("/api/info",query.data,body,type,status);
    fwrite(body.data,1,body.size,stdout);return status==200?0:1;
}
int inspection_tests(){
    int errors=0;Text text;
    if(!argument("kind=index","kind",text)||strcmp(text.data,"index"))errors++;
    Text body;const char *type="text/html";int status=200;
    if(!inspection_route("/api/info","kind=unknown",body,type,status)||status!=400)errors++;
    Text invalid;status=200;
    if(!inspection_route("/api/tests","lab=6",invalid,type,status)||status!=400)errors++;
    fprintf(stderr,"inspection helpers: %s (%d failures)\n",errors?"FAILED":"OK",errors);return errors;
}
}
