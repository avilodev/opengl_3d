#include "core/dialogue.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_OBJECT_LENGTH 8192

static char* read_all(const char* path) {
    FILE* file=fopen(path,"rb");
    if(!file)return 0;
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return 0;}
    long size=ftell(file);
    if(size<0){fclose(file);return 0;}
    rewind(file);
    char* text=malloc((size_t)size+1);
    if(!text){fclose(file);return 0;}
    if(fread(text,1,(size_t)size,file)!=(size_t)size){free(text);fclose(file);return 0;}
    text[size]=0;
    fclose(file);
    return text;
}

static const char* json_value(const char* object,const char* key) {
    char needle[80];
    snprintf(needle,sizeof(needle),"\"%s\"",key);
    const char* p=strstr(object,needle);
    if(!p)return 0;
    p+=strlen(needle);
    while(isspace((unsigned char)*p))p++;
    if(*p++!=':')return 0;
    while(isspace((unsigned char)*p))p++;
    return p;
}

static int json_string(const char*object,const char*key,char*out,int capacity) {
    const char*p=json_value(object,key);
    if(!p||*p!='\"'||capacity<=0)return 0;
    p++;
    int count=0;
    while(*p&&*p!='\"'){
        char c=*p++;
        if(c=='\\'){
            c=*p++;
            if(c=='n')c='\n';
            else if(c=='t')c='\t';
            else if(c!='\"'&&c!='\\'&&c!='/')return 0;
        }
        if(count>=capacity-1)return 0;
        out[count++]=c;
    }
    out[count]=0;
    return *p=='\"';
}

static int json_int(const char*object,const char*key,int*out) {
    const char*p=json_value(object,key);
    return p&&sscanf(p,"%d",out)==1;
}

static int json_objects(const char*json,const char*key,char out[][JSON_OBJECT_LENGTH],int capacity) {
    const char*p=json_value(json,key);
    if(!p||*p!='[')return-1;
    int count=0,depth=0,quoted=0,escaped=0;
    const char*start=0;
    for(;*p;p++){
        if(quoted){
            if(escaped)escaped=0;
            else if(*p=='\\')escaped=1;
            else if(*p=='\"')quoted=0;
            continue;
        }
        if(*p=='\"'){quoted=1;continue;}
        if(*p=='{'){if(depth==0)start=p;depth++;}
        else if(*p=='}'){
            if(depth<=0)return-1;
            depth--;
            if(depth==0&&start){
                size_t length=(size_t)(p-start+1);
                if(count>=capacity||length>=JSON_OBJECT_LENGTH)return-1;
                memcpy(out[count],start,length);out[count][length]=0;count++;start=0;
            }
        }else if(*p==']'&&depth==0)return count;
    }
    return-1;
}

void dialogue_init(DialogueState* dialogue){if(dialogue)memset(dialogue,0,sizeof(*dialogue));}

int dialogue_tree_load(DialogueTree*tree,const char*path,char*error,int error_size){
    if(!tree||!path)return 0;
    memset(tree,0,sizeof(*tree));
    char*json=read_all(path);
    if(!json){if(error&&error_size>0)snprintf(error,error_size,"could not open %s",path);return 0;}
    int version=0;
    char nodes[DIALOGUE_MAX_NODES][JSON_OBJECT_LENGTH];
    if(!json_int(json,"version",&version)||version!=1||
       !json_string(json,"id",tree->id,sizeof(tree->id))||
       !json_int(json,"start",&tree->start_node))goto invalid;
    tree->node_count=json_objects(json,"nodes",nodes,DIALOGUE_MAX_NODES);
    if(tree->node_count<=0||tree->start_node<0||tree->start_node>=tree->node_count)goto invalid;
    for(int i=0;i<tree->node_count;i++){
        DialogueNode*node=&tree->nodes[i];
        if(!json_string(nodes[i],"speaker",node->speaker,sizeof(node->speaker))||
           !json_string(nodes[i],"text",node->text,sizeof(node->text)))goto invalid;
        char choices[DIALOGUE_MAX_CHOICES][JSON_OBJECT_LENGTH];
        node->choice_count=json_objects(nodes[i],"choices",choices,DIALOGUE_MAX_CHOICES);
        if(node->choice_count<0)goto invalid;
        for(int c=0;c<node->choice_count;c++){
            DialogueChoice*choice=&node->choices[c];
            if(!json_string(choices[c],"text",choice->text,sizeof(choice->text)))goto invalid;
            const char*next=json_value(choices[c],"next");
            if(next&&strncmp(next,"null",4)==0)choice->next_node=DIALOGUE_END;
            else if(!json_int(choices[c],"next",&choice->next_node)||
                    choice->next_node<0||choice->next_node>=tree->node_count)goto invalid;
        }
    }
    free(json);return 1;
invalid:
    free(json);memset(tree,0,sizeof(*tree));
    if(error&&error_size>0)snprintf(error,error_size,"invalid dialogue file %s",path);
    return 0;
}

const DialogueNode*dialogue_current(const DialogueState*d){
    if(!d||!d->active||!d->tree||d->current_node<0||d->current_node>=d->tree->node_count)return 0;
    return&d->tree->nodes[d->current_node];
}

int dialogue_begin(DialogueState*d,const DialogueTree*tree){
    if(!d||!tree||tree->node_count<=0||tree->start_node<0||tree->start_node>=tree->node_count)return 0;
    d->tree=tree;d->current_node=tree->start_node;d->selected_choice=0;d->active=1;return 1;
}

void dialogue_close(DialogueState*d){if(d){d->active=0;d->tree=0;d->current_node=0;d->selected_choice=0;}}

int dialogue_move_selection(DialogueState*d,int direction){
    const DialogueNode*node=dialogue_current(d);
    if(!node||node->choice_count<=1||!direction)return 0;
    int next=d->selected_choice+(direction>0?1:-1);
    if(next<0)next=node->choice_count-1;
    if(next>=node->choice_count)next=0;
    d->selected_choice=next;return 1;
}

int dialogue_choose(DialogueState*d){
    const DialogueNode*node=dialogue_current(d);if(!node)return 0;
    if(node->choice_count<=0){dialogue_close(d);return 1;}
    int choice=d->selected_choice;if(choice<0||choice>=node->choice_count)choice=0;
    int next=node->choices[choice].next_node;
    if(next==DIALOGUE_END){dialogue_close(d);return 1;}
    if(next<0||!d->tree||next>=d->tree->node_count)return 0;
    d->current_node=next;d->selected_choice=0;return 1;
}

int dialogue_handle_keys(DialogueState*d,int up,int down,int accept,int cancel){
    if(!d||!d->active)return 0;
    if(cancel)dialogue_close(d);else if(up)dialogue_move_selection(d,-1);
    else if(down)dialogue_move_selection(d,1);else if(accept)dialogue_choose(d);
    return 1;
}
