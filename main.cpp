#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <memory>
#include <functional>
#include <pthread.h>
#include <iostream>
#include <deque>
#include <map>
#include <stack>
#include <vector>
using namespace std;

// define BUFLEN 10240
// typedef struct Bcsarray_t{
//    Bcs   bcs[BUFLEN/10];
//    struct Bcsarray_t  *next;
// } Bcsarray;
//
//
//
//typedef struct buffer_t{   char buf[BUFLEN];
//    Bcsarray  bcsay;
//    int bufnum;
//    int FINISH_STAGE0;
//    int START_STAGE0;
//    int FINISH_STAGE1;
//    int FINISH_STAGE2;
//    int FINISH_STAGE3;
//    int START_STAGE1;
//    int START_STAGE2;
//    int START_STAGE3;
//    Struct buffer_t  *next;
//}databuf;

enum Bcstype
{
    StagorEmptytag_start = 1,
    Etag_start,
    PI_start,
    Content,
    CDSECT_start,
    COMMENT_start,
    INVALID              = -1
};

struct START_INFO
{
    Bcstype stype;
    int fileoffset;
    int blocknum;
    int blockpos;
    int endpos;
    string attr;

    //拷贝构造函数
    START_INFO(const START_INFO & c)
    {
        stype=c.stype;
        fileoffset = c.fileoffset;
        blocknum = c.blocknum;
        blockpos = c.blockpos;
        endpos = c.endpos;
        attr = c.attr;
    }

    START_INFO(const Bcstype type, const int offset = 0, const int bnum = 0, const int bpos = 0, const int end = 0)
            :stype(type)
            ,fileoffset(offset)
            ,blocknum(bnum)
            ,blockpos(bpos)
            ,endpos(end)
            ,attr("")
    { }
};

class Xml{
public:
    Xml(string file)
            :sfile(file)
    { }

    void parseData(int i = 0, int len = 1 << 20)
    {
        //读取文件
        FILE *fpr;
        if((fpr=fopen(sfile.c_str(), "rb+")) == NULL)
        {
            cout << "open " << sfile << " error!" << endl;
            exit(-1);
        }

        char ch = '\0';
        while((ch = fgetc(fpr)) != EOF)
        {
            if( ch == '>' && !ldata.empty() && 0 == ldata.back().endpos) // 形如 <!--- <test pattern="SECAM" /><test pattern="NTSC" /> --> 需防止被覆盖
            {
                int end = ftell(fpr) - 1;

                fseek(fpr, -2, SEEK_CUR);
                string stmp(2, 0);
                fread((void*)&stmp.c_str()[0], sizeof(char), 2, fpr);
                ch = fgetc(fpr);
                if(stmp[0] == ' ' || (ch >= '0' && ch <= '9')) //内容本身的 >
                    continue;
                ldata.back().endpos = end;
            }

            if(ch == '<')
            {
                int offset = ftell(fpr) - 1;
                if( (ch = fgetc(fpr)) == ' ' || (ch >= '0' && ch <= '9'))    //内容本身的 <
                    continue;

                START_INFO tmp(Bcstype::INVALID);
                tmp.fileoffset = offset;
                tmp.blocknum = i;
                tmp.blockpos = i + offset;
                switch(ch)
                {
                    case '/':
                        tmp.stype = Etag_start;
                        break;
                    case '?':
                        tmp.stype = PI_start;
                        break;
                    case '!':
                    {
                        string stmp(8, 0);
                        int ret = fread((void*)&stmp.c_str()[0], sizeof(char), 8, fpr);
                        if(stmp.substr(0, 2) == "--")
                        {
                            tmp.stype = COMMENT_start;
                        }else if(stmp.substr(0, 7) == "[CDATA["){
                            tmp.stype = CDSECT_start;
                        }else{
                            cout << "Error===> Invalid string:" << stmp << " in pos:" << offset << endl;
                            exit(-1);
                        }
                        break;
                    }
                    default:
                        tmp.stype = StagorEmptytag_start;
                        break;
                }
                ldata.push_back(tmp);
            }
        }
        cout << endl;
        fclose(fpr);
    }

    //将读取到的标签入栈出栈
    bool checkLabel()
    {
        //读取文件
        FILE *fpr;
        if((fpr=fopen(sfile.c_str(), "r+")) == NULL)
        {
            cout << "Error===> open " << sfile << "error!" << endl;
            exit(-1);
        }
        stack<START_INFO> st;
        for(int i = 0; i < ldata.size(); ++i)
        {
            if(CDSECT_start != ldata[i].stype && COMMENT_start != ldata[i].stype && 0 == ldata[i].endpos)    // 形如 <![CDATA[  < >xx</ >  < />  ]]>    或 <!--- <test pattern="SECAM" /><test pattern="NTSC" /> -->
            {
                cout << "Error===> Invalid format, '<' in " << ldata[i].fileoffset << " without '>'" << endl;
                return false;
            }

            if((CDSECT_start == ldata[i].stype || COMMENT_start == ldata[i].stype) && 0 == ldata[i].endpos)
            {
                if(CDSECT_start == ldata[i].stype){
                    cout << " push >>> : <![CDATA[\t start: " << ldata[i].fileoffset << endl;
                }else{
                    cout << " push >>> : <!--\t start: " << ldata[i].fileoffset << endl;
                }
                st.push(ldata[i]);
                continue;
            }

            if(!st.empty() && (CDSECT_start == st.top().stype || COMMENT_start == st.top().stype))
            {
                fseek(fpr, ldata[i].endpos, SEEK_SET);    // 如果栈顶有评论和注释,则需要寻找它们的结束字段
                char ch = '\0';
                while((ch = fgetc(fpr)) != EOF){
                    if('<' == ch){
                        if(' ' == (ch = fgetc(fpr)) || (ch >= '0' && ch <= '9'))    //内容本身的 <
                            continue;
                        else
                            break;
                    }else if('>' == ch){
                        int end = ftell(fpr) - 1;
                        fseek(fpr, -3, SEEK_CUR);
                        string stmp(4, 0);
                        fread((void*)&stmp.c_str()[0], sizeof(char), 4, fpr);
                        if(' ' == stmp[1] || (stmp[3] >= '0' && stmp[3] <= '9'))    //内容本身的 >
                            continue;
                        else
                        {
                            if((CDSECT_start == st.top().stype && "]]>" == stmp.substr(0, stmp.size())) || (COMMENT_start == st.top().stype && "-->" == stmp.substr(0, stmp.size())))
                            {
                                if(CDSECT_start == ldata[i].stype){
                                    cout << " pop  --- :<![CDATA[\t start:" << ldata[i].fileoffset << " end:" << end << endl;
                                }else{
                                    cout << " pop  --- :<!--\t start:" << ldata[i].fileoffset << " end:" << end << endl;
                                }
                                st.pop();
                                break;
                            }
                        }
                    }
                }
            }

            fseek(fpr, ldata[i].endpos - 2, SEEK_SET);    // 定位至 '>' 左边二位, 读取形如 ]]> -->  ?> 等
            string stmp(3, 0);
            fread((void*)&stmp.c_str()[0], sizeof(char), 3, fpr);
            //cout << "-" <<stmp << "-"<< endl;
            if(StagorEmptytag_start == ldata[i].stype && '/' == stmp[stmp.size() - 2])
                continue;    // 形如 <dia:point val="1.95,6.85"/> 为空元素,直接跳过,不用入栈
            if(PI_start == ldata[i].stype && '?' == stmp[stmp.size() - 2])
                continue;    // 形如 <?xml version="1.0" encoding="UTF-8"?>
            if(CDSECT_start == ldata[i].stype && "]]>" == stmp)
                continue;
            if(COMMENT_start == ldata[i].stype && "-->" == stmp)
            continue;

            fseek(fpr, ldata[i].fileoffset, SEEK_SET);        // 定位至 '<'
            string att = "";
            char ch = '\0';
            do{
                ch = fgetc(fpr);
                att.append(1, ch);    // 截取属性,形如 <dia:diagramdata>   或  <dia:layer name="Background"
            }while(' ' != ch && '>' != ch);

            att[att.size() - 1] = '>';
            ldata[i].attr = att;
            if(Etag_start == ldata[i].stype)
            {
                START_INFO tp = st.top();
                string start = tp.attr.substr(1, tp.attr.size() - 1);
                if(st.empty() || (StagorEmptytag_start != tp.stype) || (StagorEmptytag_start == tp.stype && start != att.substr(2, att.size() - 2)))
                {
                    cout << "Error===> Invalid format, " << att << " in " << ldata[i].fileoffset << " without '<'" << endl;
                    return false;
                }
                cout << " pop  --- :" << tp.attr << "\t start:" << tp.fileoffset << " end:" << ldata[i].endpos << endl;
                st.pop();
                continue;
            }
            if(st.empty() || StagorEmptytag_start == ldata[i].stype)
            {
                cout << " push >>> :" << ldata[i].attr << "\t start:" << ldata[i].fileoffset << endl;
                st.push(ldata[i]);
            }else{
                cout << "Debug====> " << ldata[i].stype << endl;
            }
        }
        return st.empty();
    }
private:
    string sfile;
    vector<START_INFO> ldata;
};


int main(int argc, char* argv[])
{
    if(argc < 2)
        cout << "invalid parameter, please input your file: argv[1]" << endl;

    cout << argv[1] << endl;
    Xml myxml(argv[1]);
    myxml.parseData();
    cout << "[\n" << (bool)myxml.checkLabel() << " \n] " << endl;

    return 0;
}