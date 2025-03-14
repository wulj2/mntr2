#include <stdio.h>
#include <libgen.h>
#include <getopt.h>
#include <unistd.h>

#include "kvec.h"
#include "echarts.h"
#include "kstring.h"

typedef struct{
    const char* tbres; // mntr result log tsv file
    const char* ehtml; // output echarts file
    const char* p2ejs; // local echarts file
    int width;
    int height;
}t2e_t;

typedef kvec_t(char*) strv_t; // string vector

void t2e_head(kstring_t* s, const char* p2e, int width, int height){
    ksprintf(s, "<!DOCTYPE html>\n");
    ksprintf(s, "<html>\n");
    ksprintf(s, "<head>\n");
    ksprintf(s, "<meta charset=\"utf-8\">\n");
    ksprintf(s, "<title>Resources monitor</title>\n");
    if(p2e){
        ksprintf(s, "<script>\n");
        FILE* fre = fopen(p2e, "r");
        kgets_func* kgf = (kgets_func*)fgets;
        kstring_t* pl = (kstring_t*)calloc(1, sizeof(kstring_t));
        while(pl->l = 0, kgetline(pl, kgf, fre) >= 0){
            ksprintf(s, "%s\n", pl->s);
        }
        ksprintf(s, "</script>\n");
        fclose(fre);
        if(pl->s) free(pl->s);
        free(pl);
    }else{
        ksprintf(s, "<script>\n");
        for(unsigned int i = 0; i < echarts_min_js_len; ++i){
            ksprintf(s, "%c", echarts_min_js[i]);
        }
        ksprintf(s, "\n</script>\n");
    }
    ksprintf(s, "</head>\n");
    ksprintf(s, "<body>\n");
    ksprintf(s, "<style> .container {\n");
    ksprintf(s, "display: flex;\n");
    ksprintf(s, "justify-content: center;\n");
    ksprintf(s, "align-items: center;\n");
    ksprintf(s, "}\n");
    ksprintf(s, ".item {\n");
    ksprintf(s, "margin: auto;\n");
    ksprintf(s, "} </style>\n");
    ksprintf(s, "<div class=\"container\">\n");
    ksprintf(s, "<div class=\"item\" id=\"plotmntr\" style=\"width:%dpx;height:%dpx;\">\n", width, height);
    ksprintf(s, "</div></div>\n");
}

void t2e_js(const char* tr, kstring_t* s){
    // read in
    FILE* fr = fopen(tr, "r");
    kgets_func* kgf = (kgets_func*)fgets;
    kstring_t* pl = (kstring_t*)calloc(1, sizeof(kstring_t));
    kgetline(pl, kgf, fr); // get header
    strv_t tv, rss, shr, cpu, cmd;
    kv_init(tv), kv_init(rss), kv_init(shr), kv_init(cpu), kv_init(cmd);
    while(pl->l = 0, kgetline(pl, kgf, fr) >= 0){
        int sn = 0;
        int* sf = ksplit(pl, '\t', &sn);
        kv_push(char*, tv, strdup(pl->s));
        kv_push(char*, rss, strdup(pl->s + sf[1]));
        kv_push(char*, shr, strdup(pl->s + sf[2]));
        kv_push(char*, cpu, strdup(pl->s + sf[3]));
        kv_push(char*, cmd, strdup(pl->s + sf[4]));
        free(sf);
    }
    fclose(fr);
    if(pl->s) free(pl->s);
    free(pl);
    // to js
    ksprintf(s, "<script type=\"text/javascript\">\n");
    ksprintf(s, "\"use strict\";\n");
    ksprintf(s, "let mntrecharts_plotmntr = echarts.init(document.getElementById('plotmntr'), \"white\", {\"renderer\": \"svg\"});\n");
    ksprintf(s, "let option_plotmntr = {\n");
    ksprintf(s, "    \"color\": [\"#5470c6\", \"#91cc75\", \"#fac858\", \"#ee6666\", \"#73c0de\", \"#3ba272\", \"#fc8452\", \"#9a60b4\", \"#ea7ccc\"],\n");
    ksprintf(s, "    \"dataZoom\": [{\"type\": \"\", \"start\": 0, \"end\": 100, \"xAxisIndex\": [0]}],\n");
    ksprintf(s, "    \"legend\": {\"show\": true, \"selectedMode\": \"multiple\"},\n");
    ksprintf(s, "    \"title\": {\"text\": \"\", \"subtext\": \"\"},\n");
    ksprintf(s, "    \"tooltip\": {\"show\": true, \"trigger\": \"axis\", \"axisPointer\": {\"type\": \"cross\", \"snap\": true}},\n");
    // series
    ksprintf(s, "    \"series\": [");
    // cpu data
    ksprintf(s, "        {\"name\": \"CPU(%%)\",\n");
    ksprintf(s, "        \"type\": \"line\",\n");
    ksprintf(s, "        \"smooth\": true,\n");
    ksprintf(s, "        \"waveAnimation\": false,\n");
    ksprintf(s, "        \"renderLabelForZeroData\": false,\n");
    ksprintf(s, "        \"selectedMode\": false,\n");
    ksprintf(s, "        \"animation\": false,\n");
    ksprintf(s, "        \"data\": [");
    for(size_t i = 0; i < cpu.n; ++i){
        ksprintf(s, "{\"value\": %s, \"XAxisIndex\": 0, \"YAxisIndex\": 0},", cpu.a[i]);
    }
    s->s[s->l-1] = ']';
    ksprintf(s, "},\n");
    kv_destroy(cpu);
    // shr data
    ksprintf(s, "        {\"name\": \"SHR\",\n");
    ksprintf(s, "        \"type\": \"line\",\n");
    ksprintf(s, "        \"yAxisIndex\": 1,\n");
    ksprintf(s, "        \"smooth\": true,\n");
    ksprintf(s, "        \"waveAnimation\": false,\n");
    ksprintf(s, "        \"renderLabelForZeroData\": false,\n");
    ksprintf(s, "        \"selectedMode\": false,\n");
    ksprintf(s, "        \"animation\": false,\n");
    ksprintf(s, "        \"data\": [");
    for(size_t i = 0; i < shr.n; ++i){
        ksprintf(s, "{\"value\": %s, \"XAxisIndex\": 0, \"YAxisIndex\": 1},", shr.a[i]);
    }
    s->s[s->l-1] = ']';
    ksprintf(s, "},\n");
    kv_destroy(shr);
    // rss data
    ksprintf(s, "        {\"name\": \"RSS\",\n");
    ksprintf(s, "        \"type\": \"line\",\n");
    ksprintf(s, "        \"yAxisIndex\": 1,\n");
    ksprintf(s, "        \"smooth\": true,\n");
    ksprintf(s, "        \"waveAnimation\": false,\n");
    ksprintf(s, "        \"renderLabelForZeroData\": false,\n");
    ksprintf(s, "        \"selectedMode\": false,\n");
    ksprintf(s, "        \"animation\": false,\n");
    ksprintf(s, "        \"data\": [");
    for(size_t i = 0; i < rss.n; ++i){
        ksprintf(s, "{\"value\": %s, \"XAxisIndex\": 0, \"YAxisIndex\": 1},", rss.a[i]);
    }
    s->s[s->l-1] = ']';
    ksprintf(s, "},\n");
    kv_destroy(rss);
    // cmd data
    ksprintf(s, "        {\"name\": \"CMD\",\n");
    ksprintf(s, "        \"type\": \"line\",\n");
    ksprintf(s, "        \"yAxisIndex\": 1,\n");
    ksprintf(s, "        \"smooth\": true,\n");
    ksprintf(s, "        \"waveAnimation\": false,\n");
    ksprintf(s, "        \"renderLabelForZeroData\": false,\n");
    ksprintf(s, "        \"selectedMode\": false,\n");
    ksprintf(s, "        \"animation\": false,\n");
    ksprintf(s, "        \"data\": [");
    for(size_t i = 0; i < cmd.n; ++i){
        ksprintf(s, "{\"value\": \"%s\", \"XAxisIndex\": 0, \"YAxisIndex\": 1},", cmd.a[i]);
    }
    s->s[s->l-1] = ']';
    ksprintf(s, "}],\n");
    kv_destroy(cmd);
    // time data
    ksprintf(s, "\"xAxis\": [{\"data\":[\n");
    for(size_t i = 0; i < tv.n; ++i){
        ksprintf(s, "\"%s\",", tv.a[i]);
    }
    s->s[s->l-1] = ']';
    ksprintf(s, "}],\n");
    kv_destroy(tv);
    // yaxis
    ksprintf(s, "\"yAxis\": [{\"name\": \"CPU(%%)\", \"type\": \"value\", \"show\": true, \"scale\": true},\n");
    ksprintf(s, "            {\"name\": \"Memory(G)\", \"type\": \"value\", \"show\": true, \"scale\": true}],\n");
    // toolbox
    ksprintf(s, "\"toolbox\": {\"show\": true, \"feature\":{\n");
    ksprintf(s, "\"saveAsImage\":{\"type\": 'svg', \"name\": 'mntr.svg'},\n");
    ksprintf(s, "\"restore\":{\"show\": true},\n");
    ksprintf(s, "\"dataZoom\":{\"show\": true},\n");
    ksprintf(s, "\"magicType\": {\"type\": ['line', 'bar']}\n");
    ksprintf(s, "}}\n");
    // end js
    ksprintf(s, "};\nmntrecharts_plotmntr.setOption(option_plotmntr);\n</script>\n");
    // end html
    ksprintf(s, "</body></html>\n");
}

void t2e_html(t2e_t* opt){
    kstring_t s = {0, 0, 0};
    t2e_head(&s, opt->p2ejs, opt->width, opt->height);
    t2e_js(opt->tbres, &s);
    FILE* fw = fopen(opt->ehtml, "w");
    fwrite(s.s, sizeof(char), s.l, fw);
    fclose(fw);
    if(s.s) free(s.s);
}
