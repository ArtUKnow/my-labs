#include "examples.h"
#include "corpus.h"
#include "crawler.h"
#include "index.h"
#include "inspection.h"
#include "text.h"
#include <errno.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace web {
namespace {
struct Example {
    const char *id;
    int lab;
    const char *title, *description, *input;
    const char *args[7];
    bool sample;
    const char *result_file;
};
const Example examples[] = {
    {"corpus-size",1,"Объём корпуса","Измерить количество и размер сжатых текстов. Размер распакованных текстов — сохранённый результат.",NULL,{"inspect","corpus",NULL},false,NULL},
    {"corpus-paths",1,"Подготовка каталога","Проверить пути всех статей. Результат записывается в отдельный файл примеров.",NULL,{"prepare","lab01_corpus/data/provenance/documents.tsv","build/examples/all_documents.tsv",NULL},false,NULL},
    {"crawler-test",2,"Робот: 200, 304, обновление","Настоящий локальный HTTP-тест: загрузка, отсутствие изменений, обновление. Весь корпус повторно не скачивается.",NULL,{"test-crawler",NULL},false,NULL},
    {"crawler-state",2,"Итог загрузки статей","Прочитать сохранённый итог полного обхода: статьи, оригиналы, HTML, очередь и ошибки.",NULL,{"inspect","crawler",NULL},false,NULL},
    {"tokens-words",3,"Слова, регистр, пунктуация","Текст передаётся в стандартный ввод. В выводе — токены в нижнем регистре.","Dark matter, частицы и Союз-2: 3.14 GeV!\n",{"tokenize",NULL},false,NULL},
    {"tokens-numbers",3,"Числа и разделители","Проверить десятичные числа, дефис, точку и апостроф.","3.14 1,25 x.y Союз-2 O’Neill 42\n",{"tokenize",NULL},false,NULL},
    {"tokens-unicode",3,"Разные алфавиты","Кириллица, греческие буквы, китайские символы, арабские цифры.","РАКЕТА ЁЖ ΑΒ 中文 ١٢٣\n",{"tokenize",NULL},false,NULL},
    {"tokens-tests",3,"Юнит-тесты токенизации","Выполнить отдельные тесты токенизатора, включая границы токенов и Unicode.",NULL,{"test-tokenizer",NULL},false,NULL},
    {"stem-russian",4,"Русские формы слов","Одна строка — одно слово. Справа показывается результат стемминга.","ракеты\nспутников\nмашинами\nёлки\n",{"stem",NULL},false,NULL},
    {"stem-english",4,"Английские формы слов","Сравнить основы слов с разными окончаниями.","particles\nrunning\nclasses\nstudies\n",{"stem",NULL},false,NULL},
    {"lemma-words",4,"Правила лемматизации","Посмотреть результат имеющихся словарных исключений и правил.","went\nchildren\nдети\nhas\n",{"lemma",NULL},false,NULL},
    {"stem-tests",4,"Юнит-тесты стемминга","Отдельные тесты русского и английского стемминга, правил лемматизации.",NULL,{"test-stemmer",NULL},false,NULL},
    {"zipf-sample",5,"Частоты на 100 статьях","Новый быстрый расчёт на первых 100 статьях. JSON и SVG записываются отдельно в build/examples.",NULL,{"zipf","build/examples/articles_100.tsv","build/examples/zipf.json","build/examples/zipf.svg",NULL},true,"build/examples/zipf.json"},
    {"zipf-full",5,"Распределение полного корпуса","Прочитать сохранённые частоты и параметры закона Ципфа для всего корпуса.",NULL,{"inspect","zipf",NULL},false,NULL},
    {"index-stem",7,"Построить индекс: стемминг","Реальная сборка отдельного индекса 100 статей, блок — 10 000 токенов.",NULL,{"build","build/examples/articles_100.tsv","build/examples/index_stem","stem","10000",NULL},true,"build/examples/index_stem/stats.json"},
    {"index-raw",7,"Построить индекс: без стемминга","Те же 100 статей, исходные токены. Можно сравнить размер словаря.",NULL,{"build","build/examples/articles_100.tsv","build/examples/index_raw","raw","10000",NULL},true,"build/examples/index_raw/stats.json"},
    {"index-size",7,"Размер файлов полного индекса","Измерить dictionary.bin, postings.bin, documents.bin, meta.bin и архив.",NULL,{"inspect","index",NULL},false,NULL},
    {"index-counts",7,"Термины и постинги","Параметры сохранённой полной сборки индекса.",NULL,{"inspect","terms",NULL},false,NULL},
    {"search-and",8,"И: тёмная материя","Пробел между словами означает И. Поиск по полному индексу, первые пять статей.","dark matter\n",{"search","lab01_corpus/data/native_index","--limit","5",NULL},false,NULL},
    {"search-or",8,"ИЛИ и скобки","Статьи с neutrino или quark, одновременно содержащие mass.","(neutrino || quark) mass\n",{"search","lab01_corpus/data/native_index","--limit","5",NULL},false,NULL},
    {"search-not",8,"Исключение слова","Статьи с higgs без boson.","higgs !boson\n",{"search","lab01_corpus/data/native_index","--limit","5",NULL},false,NULL},
    {"search-nested",8,"Вложенное выражение","Сочетание скобок, ИЛИ и отрицания по полному корпусу.","((dark matter) || (higgs boson)) !supersymmetry\n",{"search","lab01_corpus/data/native_index","--limit","5",NULL},false,NULL}
};
void quote(Text &out,const char *text) {
    out.letter('"');for(;*text;++text){unsigned char c=(unsigned char)*text;
        if(c=='"'||c=='\\'){out.letter('\\');out.letter(c);}else if(c<32)out.format("\\u%04x",c);else out.letter(c);
    }out.letter('"');
}
bool parameter(const char *parameters,const char *name,Text &value){
    if(!parameters)return false;
    for(const char *part=parameters;*part;){
        const char *end=strchr(part,'&');if(!end)end=part+strlen(part);
        const char *equal=(const char*)memchr(part,'=',end-part);
        if(equal&&(size_t)(equal-part)==strlen(name)&&!strncmp(part,name,equal-part))return decode_url(equal+1,end-equal-1,value);
        part=*end?end+1:end;
    }return false;
}
const Example *find_example(const char *id){for(const Example &example:examples)if(!strcmp(example.id,id))return &example;return NULL;}
void shell_quote(Text &out,const char *text){
    out.letter('\'');for(;*text;++text){if(*text=='\'')out.add("'\\''");else out.letter(*text);}out.letter('\'');
}
void command(const Example &example,Text &out){
    if(!strcmp(example.args[0],"inspect")){inspection_command(example.args[1],out);return;}
    if(example.sample || !strcmp(example.id,"corpus-paths"))out.add("mkdir -p build/examples\n");
    if(example.sample)out.add("head -n 101 lab01_corpus/data/native_documents.tsv > build/examples/articles_100.tsv\n");
    if(example.input){out.add("printf '%s' ");shell_quote(out,example.input);out.add(" | ");}
    out.add("./build/ir");for(int i=0;example.args[i];++i){out.letter(' ');out.add(example.args[i]);}
    if(example.result_file){out.add("\ncat ");out.add(example.result_file);}
}
bool make_directory(const char *path){if(!mkdir(path,0755))return true;struct stat info;return errno==EEXIST&&!stat(path,&info)&&S_ISDIR(info.st_mode);}
bool sample_manifest(){
    FILE *input=fopen("lab01_corpus/data/native_documents.tsv","r");if(!input)return false;
    FILE *output=fopen("build/examples/articles_100.tsv","w");if(!output){fclose(input);return false;}
    char *line=NULL;size_t capacity=0;bool ok=true;
    for(int i=0;i<101;++i){ssize_t count=getline(&line,&capacity,input);if(count<0||fwrite(line,1,(size_t)count,output)!=(size_t)count){ok=false;break;}}
    free(line);if(ferror(input))ok=false;fclose(input);if(fclose(output))ok=false;return ok;
}
int dispatch(const Example &example){
    char *args[7];int count=0;while(example.args[count]){args[count]=(char*)example.args[count];++count;}
    args[count]=NULL;const char *name=args[0];
    if(!strcmp(name,"inspect"))return ir_inspection_main(count,args);
    if(!strcmp(name,"prepare"))return ir_prepare_main(count,args);
    if(!strcmp(name,"test-crawler"))return ir_crawler_tests();
    if(!strcmp(name,"test-tokenizer"))return ir_tokenizer_tests();
    if(!strcmp(name,"test-stemmer"))return ir_stemmer_tests();
    if(!strcmp(name,"tokenize"))return ir_tokenizer_main(count,args);
    if(!strcmp(name,"stem")||!strcmp(name,"lemma"))return ir_stem_main(count,args);
    if(!strcmp(name,"zipf"))return ir_zipf_main(count,args);
    if(!strcmp(name,"build"))return ir_index_main(count,args);
    if(!strcmp(name,"search"))return ir_search_main(count,args);
    fprintf(stderr,"Unknown example command\n");return 2;
}
int execute(const Example &example,Text &body){
    FILE *input=tmpfile(),*output=tmpfile();
    if(!input||!output){if(input)fclose(input);if(output)fclose(output);body.add("{\"ok\":false,\"output\":\"Не удалось создать файл вывода\"}");return 500;}
    if(example.input)fwrite(example.input,1,strlen(example.input),input);
    rewind(input);fflush(NULL);timespec start,finish;clock_gettime(CLOCK_MONOTONIC,&start);
    pid_t child=fork();
    if(child<0){fclose(input);fclose(output);body.add("{\"ok\":false,\"output\":\"Не удалось создать процесс\"}");return 500;}
    if(!child){
        alarm(30);dup2(fileno(input),STDIN_FILENO);dup2(fileno(output),STDOUT_FILENO);dup2(fileno(output),STDERR_FILENO);
        clearerr(stdin);clearerr(stdout);clearerr(stderr);
        bool ready=make_directory("build")&&make_directory("build/examples")&&(!example.sample||sample_manifest());
        int result=1;
        if(!ready)fprintf(stderr,"Cannot prepare example files\n");
        else result=dispatch(example);
        if(!result&&example.result_file){
            FILE *file=fopen(example.result_file,"r");
            if(!file){perror(example.result_file);result=1;}
            else{char buffer[4096];size_t n;while((n=fread(buffer,1,sizeof buffer,file)))fwrite(buffer,1,n,stdout);if(ferror(file))result=1;fclose(file);}
        }
        fflush(stdout);fflush(stderr);_exit(result);
    }
    fclose(input);int result=0;rusage usage={};pid_t waited;
    do{waited=wait4(child,&result,0,&usage);}while(waited<0&&errno==EINTR);
    clock_gettime(CLOCK_MONOTONIC,&finish);rewind(output);Text text;char buffer[4096];size_t n;
    while(text.size<256*1024&&(n=fread(buffer,1,sizeof buffer,output)))text.add(buffer,n);
    fclose(output);bool ok=waited==child&&WIFEXITED(result)&&WEXITSTATUS(result)==0;
    if(!ok&&!text.size)text.add("Процесс завершился с ошибкой или превысил лимит 30 секунд.\n");
    uint64_t peak=usage.ru_maxrss;
#ifndef __APPLE__
    peak*=1024;
#endif
    double elapsed=(finish.tv_sec-start.tv_sec)*1000.+(finish.tv_nsec-start.tv_nsec)/1000000.;
    Text shown;command(example,shown);
    body.format("{\"ok\":%s,\"elapsed_ms\":%.3f,\"peak_rss_bytes\":%llu,\"command\":",ok?"true":"false",elapsed,(unsigned long long)peak);
    quote(body,shown.data);body.add(",\"output\":");quote(body,text.data);body.add("}");return ok?200:500;
}
}
bool examples_route(const char *path,const char *parameters,Text &body,const char *&type,int &status){
    if(!strcmp(path,"/api/execute")){
        type="application/json; charset=utf-8";Text id;
        const Example *example=parameter(parameters,"example",id)?find_example(id.data):NULL;
        if(!example){status=400;body.add("{\"ok\":false,\"output\":\"Неизвестный пример\"}");}else status=execute(*example,body);
        return true;
    }
    if(strcmp(path,"/api/examples"))return false;
    type="application/json; charset=utf-8";Text lab;int number=0;
    if(parameter(parameters,"lab",lab)){
        char *end=NULL;long parsed=strtol(lab.data,&end,10);
        if(*lab.data&&!*end&&parsed>=1&&parsed<=8)number=(int)parsed;
    }
    body.letter('[');bool first=true;
    for(const Example &example:examples)if(example.lab==number){
        if(!first)body.letter(',');first=false;Text shown;command(example,shown);
        body.add("{\"id\":");quote(body,example.id);body.add(",\"title\":");quote(body,example.title);
        body.add(",\"description\":");quote(body,example.description);body.add(",\"command\":");quote(body,shown.data);body.letter('}');
    }
    body.letter(']');if(first)status=400;return true;
}
int examples_tests(){
    int errors=0;
    if(find_example("rm -rf /"))++errors;
    for(size_t i=0;i<sizeof examples/sizeof *examples;++i){
        const Example &example=examples[i];
        if(example.lab==6)++errors;
        for(size_t j=0;j<i;++j)if(!strcmp(example.id,examples[j].id))++errors;
    }
    Text quoted;shell_quote(quoted,"a'b");if(strcmp(quoted.data,"'a'\\''b'"))++errors;
    Text body;const char *type="";int status=200;
    if(!examples_route("/api/execute","example=unknown",body,type,status)||status!=400)++errors;
    fprintf(stderr,"example helpers: %s (%d failures)\n",errors?"FAILED":"OK",errors);return errors;
}
}
