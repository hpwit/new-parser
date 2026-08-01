#include "stackfunctions.h"
#include "parser_define.h"
#include "string_constants.h"
#include <stdio.h>

#include "string.h"
//#define __SPEED

    int Text::findText(char * str)
    {
         #ifdef __SPEED
        return -1;
        #endif
       // for (int i = 0; i < _texts.size(); i++)
       for (int i = 0; i <_texts.size(); i++)
        {
            if (strcmp(str,_texts[i]) == 0)
            {
                return i;
            }
        }
        return -1;
    }
    int Text::addText(char * str,uint16_t si)
    {
            char *   m = (char *)malloc(si + 1);
        memcpy(m, str, si);
        m[si] = 0;
        _texts.push_back(m);
        position++;
      // free(str);
        return _texts.size() - 1;
    }
    int Text::addText(const char *str)
    {
        char *   m = (char *)malloc(strlen(str)+1);
        memcpy(m, str, strlen(str));
        m[strlen(str)] = 0;
       return addText(m);
    }
    int Text::addText(char* str,char * e)
    { 
        assert(e>=str);
        char *   m = (char *)malloc(e-str+2);
        memcpy(m, str, e-str+1);
        m[e-str+1] = 0;

       return addText(m);
        
    }
        int Text::addTextNoDelete(char* str)
    {
        int pos = findText(str);
        if (pos > -1)
        {
            if(str!=_texts[pos])
             free(str);
            return pos;
           
        }

        _texts.push_back(str);
        position++;
        return _texts.size() - 1;
    }
    int Text::addText(char* str)
    {
        int pos = findText(str);
        if (pos > -1)
        {
             if(str!=_texts[pos])
             free(str);
            return pos;
           
        }

        _texts.push_back(str);
        position++;
        return _texts.size() - 1;
    }
    void Text::addAfter(int pos, char * s)
    {
        _it = getChildAtPos(pos);
        // printf(" on recupere %d:%s\n",pos,(*__it).c_str());
        // if((*_it).compare(s)!=0)
        //{
        addAfter(s);
        position--;
        _it = getChildAtPos(position);
        position++;
        // }
    }

    void Text::addBefore(int pos, char * s)
    {
        _it = getChildAtPos(pos-1);
        // printf(" on recupere %d:%s\n",pos,(*__it).c_str());
        // if((*_it).compare(s)!=0)
        //{
        addBefore(s);
        position--;
        _it = getChildAtPos(position);
        position++;
        // }
    }
   void Text::addAfter(const char * str)
   {
    char *m=(char *)malloc(strlen(str)+1);
    memcpy(m,str,strlen(str));
    m[strlen(str)]=0;
    addAfter(m);
   }
    void Text::addAfter(char * str)
    {
        int pos = findText(str);
        char *m;
        if (pos > -1)
        {
            m = _texts[pos];
            free(str);
        }
        else
        {
            m=str;  
        }
        if (_it == _texts.end())
        {
           
            _texts.push_back(m);
            _it = _texts.end();
            _it--;
        }
        else
        {
 
            _it = _texts.insertAfter(_it, m);
        }
        position++;
    }
    char* Text::back()
    {
              if (_texts.size() > 0)
            return _texts.back();
        else
            return _end_text;
    }
    char * Text::current()
    {
        return *_it;
    }
    void Text::blankCurrent()
    {
        int lastIdx = _texts.size() - 1;
        char *old = _texts.get(lastIdx);

        // Look up a pooled " " before freeing old -- findText() scans the
        // whole pool, including this slot's current entry, so freeing it
        // first would leave that scan reading a dangling pointer.
        int pos = findText((char *)" ");
        char *m;
        if (pos == lastIdx)
        {
            // Already blank -- nothing to do.
            return;
        }
        else if (pos > -1)
        {
            m = _texts.get(pos);
        }
        else
        {
            m = (char *)malloc(2);
            m[0] = 32;
            m[1] = 0;
        }

        if (!isReused(lastIdx))
            free(old);
        *_it = m;
    }
    char * Text::front()
    {
        if (_texts.size() > 0)
            return _texts.front();
        else
            return _end_text;
    }
    void Text::pop_front()
    {
        if (_texts.size() > 0)
        {
            if(_texts.front() !=NULL)
            {
              // printf("we tray to look to  delete:|%s|\n",_texts.front());
            if (!isReused(0))
            {
             //printf("we tray to delete:|%s|\n",_texts.front());
              //(_texts.front());
               
              //printf("we delted the string\n\r");
            }
            }
            // _texts[0]=NULL;
            _texts.erase(_texts.begin());
        }
    }
    void Text::addAfterNoDouble(char* s)
    {

       // char *str;
        if (_it != _texts.end())
        {

            if (strcmp(*_it,s) == 0)
            {

                return;
            }
  

        }

        addAfter(s);
    }
    void Text::addBefore(char * s)
    {
        int pos = findText(s);
        if (pos > -1)
        {
            free(s);
            _it = _texts.insertBefore(_it, _texts.get(pos));
        }
        else
        {
           
            _it = _texts.insertBefore(_it, s);
        }
        _it++;
        position++;
    }
    void Text::addBefore(const char * s)
    {
        char *m=(char *)malloc(strlen(s)+1);
        memcpy(m,s,strlen(s));
        m[strlen(s)]=0;
        addBefore(m);
    }

    void Text::replaceText(int pos, char * str)
    {
        if (pos >= 0 and pos < size())
        {
            // Look up a pooled duplicate and check reuse before freeing
            // anything -- both scan the whole pool, including this slot's
            // current (about-to-be-replaced) entry, so freeing it first
            // would leave them reading a dangling pointer.
            char *old = _texts.get(pos);
            int existing = findText(str);
            if (existing > -1 && existing != pos)
            {
                free(str);
                str = _texts.get(existing);
            }
            if (!isReused(pos))
                free(old);
            *_texts.getptr(pos) = str;
        }
        else
        {
            free(str);
        }
    }
    void Text::replaceText(int pos, const char * str)
    {
        char *m = (char *)malloc(strlen(str) + 1);
        memcpy(m, str, strlen(str));
        m[strlen(str)] = 0;
        replaceText(pos, m);
    }
    char ** Text::getChildAtPos(int pos)
    {
        
        if (pos >= _texts.size() || pos < 0)
        {
            return _texts.end();
        }
        else
        {
             return _texts.getptr(pos);
        }
        return _texts.end();
    }
    void Text::putIteratorAtPos(int pos)
    {
        _it = getChildAtPos(pos);
        //position=pos-1;
    }
    void Text::end()
    {
        _it = getChildAtPos(_texts.size() - 1);
    }
    void Text::clear()
    {
        /*
         for (int i = 0; i < _texts.size(); i++)
        {
            char *c1 = _texts.get(i);
            if (c1 != NULL)
            {
                if (i < _texts.size() - 2)
                {
                    for (int j = i + 1; j < _texts.size(); j++)
                    {
                        if (_texts.get(j) == c1)
                            *(_texts.getptr(j)) = NULL;
                    }
                }
            }
        }
//#endif
        for (int i = 0; i < _texts.size(); i++)
        {
            if (_texts.get(i) != NULL)
            {
                printf("%s \n",_texts.get(i));
                free(_texts.get(i));
               // kk++;
            }
        }
*/
        
       _texts.empty();
       sp.clear();
       position = 0;
       _it = _texts.begin();
    }
    int Text::size()
    {
        return _texts.size();
    }
    char *Text::getText(int pos)
    {
        if (pos >= 0 and pos < _texts.size())
        {
            
            return _texts[pos];
        }
        else
        {
            return _end_text;
        }
    }
    bool Text::isReused(int pos)
    {
        #ifdef __SPEED
        return false;
        #endif
        if (pos < 0 or pos >= _texts.size())
        {
            return false;
        }
        char *c = _texts[pos];
        for (int i = 0; i < _texts.size(); i++)
        {
            if (i != pos && c== _texts[i])
            {
                return true;
            }
        }
        return false;
    }
    void Text::pop()
    {
        if (size() > 0)
        {
            if (!isReused(_texts.size() - 1))
            {
               free(_texts.back());
            }
            _texts.pop_back();
           // _texts.shrink_to_fit();
            position--;
            _it = _texts.end();
            _it--;
        }
    }
    int Text::get()
    {

        return position - 1;
    }
    void Text::begin()
    {
        _it = _texts.begin();
        position = 0;
    }
    void Text::display()
    {
       for(int i=0;i<_texts.size();i++)
       {
        printf("tes %d:%s \n",i,_texts.get(i));
       }
    }
