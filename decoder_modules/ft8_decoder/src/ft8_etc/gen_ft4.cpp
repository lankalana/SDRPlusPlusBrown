/* The algorithms, source code, look-and-feel of WSJT-X and related
 * programs, and protocol specifications for the modes FSK441, FT8, JT4,
 * JT6M, JT9, JT65, JTMS, QRA64, ISCAT, MSK144, are Copyright © 2001-2017
 * by one or more of the following authors: Joseph Taylor, K1JT; Bill
 * Somerville, G4WJS; Steven Franke, K9AN; Nico Palermo, IV3NWV; Greg Beam,
 * KI7MT; Michael Black, W9MDB; Edson Pereira, PY2SDR; Philip Karn, KA9Q;
 * and other members of the WSJT Development Group.
 *
 * MSHV FT4 Codec
 * Rewritten into C++ and modified by Hrisimir Hristov, LZ2HV 2015-2019
 * May be used under the terms of the GNU General Public License (GPL)
 */

#include "gen_ft4.h"
//#include <QtGui>

GenFt4::GenFt4(bool f_dec_gen)//f_dec_gen = dec=true gen=false
{
    TPackUnpackMsg77.initPackUnpack77(f_dec_gen);//f_dec_gen = dec=true gen=false
    genPomFt.initGenPomFt();//first_ft4_enc_174_91 = true;
}
GenFt4::~GenFt4()
{}
/*
void GenFt4::save_hash_call_from_dec(QString c13,int n10,int n12,int n22)
{
    TPackUnpackMsg77.save_hash_call(c13,n10,n12,n22);
}
void GenFt4::save_hash_call_my_his_r1_r2(QString call,int pos)
{
    TPackUnpackMsg77.save_hash_call_my_his_r1_r2(call,pos);
}
*/
void GenFt4::save_hash_call_mam(QStringList ls)
{
    TPackUnpackMsg77.save_hash_call_mam(ls);
}
QString GenFt4::unpack77(bool *c77,bool &unpk77_success)
{
    return TPackUnpackMsg77.unpack77(c77,unpk77_success);
}
void GenFt4::pack77(QString msgs,int &i3,int n3,bool *c77)// for apset v2
{
    TPackUnpackMsg77.pack77(msgs,i3,n3,c77);
}
void GenFt4::encode174_91(bool *message77,bool *codeword)
{
	genPomFt.encode174_91(message77,codeword);
}
void GenFt4::make_c77_i4tone(bool *c77,int *i4tone)//,bool f_gen,bool f_addc
{	
    const int icos4a[4]={0,1,3,2};
    const int icos4b[4]={1,0,2,3};
    const int icos4c[4]={2,3,1,0};
    const int icos4d[4]={3,2,0,1};
    const bool rvec[77]={0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0,
                   1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1,
                   0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1};
    int itmp[92];//(ND=87)               
                   
    bool codeword[180];//3*58+5 linux subtract error
    //bool *codeword = new bool[180];//3*58+5  full=174             
    bool cc77[180]; // w10 32bit error
    //bool *cc77 = new bool[100]; // w10 32bit error
    for (int i= 0; i < 176; ++i)
    {
    	if (i<77) 
    		cc77[i]=c77[i];
    	else 
    		cc77[i] = 0;
    	codeword[i] = 0;
   	} 
                  
    for (int i= 0; i < 77; ++i)
        cc77[i]=fmod(cc77[i]+rvec[i],2);  //msgbits=mod(msgbits+rvec,2)
    genPomFt.encode174_91(cc77,codeword);

    for (int i= 0; i < 87; ++i)//c++   ==.EQ. !=.NE. >.GT. <.LT. >=.GE. <=.LE.
    {   // do i=1,ND=87
        int is=codeword[2*i+1]+2*codeword[2*i];//is=codeword(2*i=2,4)+2*codeword(2*i-1)=1,3
        /*if (is<=1) itmp[i]=is;
        if (is==2) itmp[i]=3;
        if (is==3) itmp[i]=2;*/
        if      (is==1) itmp[i]=1;
        else if (is==2) itmp[i]=3;
        else if (is==3) itmp[i]=2;
        else            itmp[i]=0;      	
    }

    for (int i= 0; i < 4; ++i)   //i4tone(1:4)=icos4a
        i4tone[i] = icos4a[i];
    for (int i= 0; i < 29; ++i)
        i4tone[i+4]=itmp[i];     //i4tone(5:33)=itmp(1:29)
    for (int i= 0; i < 4; ++i)
        i4tone[i+33]=icos4b[i];  //i4tone(34:37)=icos4b
    for (int i= 0; i < 29; ++i)
        i4tone[i+37]=itmp[i+29]; //i4tone(38:66)=itmp(30:58)
    for (int i= 0; i < 4; ++i)
        i4tone[i+66]=icos4c[i];  //i4tone(67:70)=icos4c
    for (int i= 0; i < 29; ++i)
        i4tone[i+70]=itmp[i+58]; //i4tone(71:99)=itmp(59:87)
    for (int i= 0; i < 4; ++i)
        i4tone[i+99]=icos4d[i];  //i4tone(100:103)=icos4d
        
    //delete cc77;
    //delete codeword;
}
