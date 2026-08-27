/* MSHV DecoderMs
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#ifndef DECODERMS_H
#define DECODERMS_H


#include "mshv_support.h"

#include "decoderpom.h"
#include "gen_ft4.h"
#include "gen_ft8.h"
#include "ctm.h"

#include <iostream>
#include <chrono>
#include <functional>
#include <array>
#include <atomic>

// #include "../HvMsPlayer/libsound/HvGenFt8/gen_ft8.h"
//#include <QObject> //2.53
#define ALL_MSG_SNR 120 //2.63 from 100 to 120
#define MAXDEC 120
class DecoderFt8
{
    int outCount = 0;
public:
    explicit DecoderFt8(int id, std::shared_ptr<F2a> f2a);
    ~DecoderFt8();
    void SetStMultiAnswerMod(bool f);
    void SetStWords(QString,QString,int,int);
    void SetStHisCall(QString s1);
    void SetStDecode(QString time,int mousebutton,bool);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);
    void SetStQSOProgress(int i);
    void SetStTxFreq(double f);
    void Decode3intFt(bool);//2.39 remm
    void SetNewP(bool);
    //void SetResetPrevT(QString ptime);
    void ft8_decode(double *dd,int c_dd,double f0a,double f0b,double fqso,bool &f,int id3dec,double,double);
    void SetResultsCallback(std::function<void(const char *)> fun) {
        this->resultsCallback = fun;
    }


//signals:
    void EmitDecodedTextFt(QStringList lst);
    void EmitBackColor() {
//        abort();
    }

private:
    int decid;
    std::function<void(const char *)> resultsCallback;

    std::shared_ptr<F2a> f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt8 *TGenFt8;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;

    bool f_new_p;
    //QString s_time8_prev;
    int s_ndecodes;
    QString allmessages[ALL_MSG_SNR+20];
    //int allsnrs[ALL_MSG_SNR+20];
    double f1_save[ALL_MSG_SNR+20];
    double xdt_save[ALL_MSG_SNR+20];
    int itone_save[ALL_MSG_SNR+20][100];
    bool lsubtracted[ALL_MSG_SNR+20];
    int s_cou_dd1;
    double dd1[182600];

    bool first_sync8d;
    std::complex<double> csync_ft8_2[7][32];
    void sync8d(std::complex<double> *cd0,int i0,std::complex<double> *ctwk,int itwk,double &sync);

    bool first_ft8_downsample;
    double taper_ft8_ds[120];
    std::complex<double> cx_ft8[192100];//2.09 ->error //96000+100   //(0:NFFT1/2)  NFFT1=192000 // 96000
    void ft8_downsample(double *dd0,bool &newdat,double f1,std::complex<double> *cd0);

    double pulse_ft8_rx[5770];          //    !1920*3=5760
    std::complex<double> ctab8_[65536+10];
    void gen_ft8cwaveRx(int *i4tone,double f_tx,std::complex<double> *cwave);

    bool first_subsft8;
    std::complex<double> cw_subsft8[180300];//180000+300  15*12000
    double endcorrectionft8[2200]; //(NFILT/2+1)  NFILT=4000; 4000/2+1=2001
    double BestIdtft8(double *dd,double f0,double dt,double idt,std::complex<double> *cref,
                      std::complex<double> *cfilt,std::complex<double> *cw_subs,double *endcorr,
                      double *xdd,std::complex<double> *cx);
    void subtractft8(double *dd,int *itone,double f0,double dt,bool refdt);

    bool first_ft8b_2;
    int mcq_ft8_2[29];
    /*int mcqfd_ft8_2[29];
    int mcqru_ft8_2[29];
    int mcqtest_ft8_2[29];
    int mcqww_ft8_2[29];
    int mcqbu_ft8_2[29];
    int mcqft_ft8_2[29];*/
    int mrrr_ft8_2[19];
    int m73_ft8_2[19];
    int mrr73_ft8_2[19];
    int cont_id0_ft8_2;
    bool one_ft8_2[9][512];//(0:511,0:8);
    QString hiscall12_0_ft8_2;
    bool ft8_downs_sync_bmet(double *,bool ap7,bool &,double &,double &,int &,int &,double s8_[79][8],
                             double *,double *,double *,double *);
    void ft8b(double *dd,bool &newdat,int nQSOProgress,double nfqso,double nftx,int ndepth,bool n4pas3int,bool lapon,
              double napwid,bool lsubtract,bool nagain,int cid,int cty,int &iaptype,double &f1,double &xdt,
              double xbase,int *apsym,int &nharderrors,double &dmin,int &nbadcrc,QString &message,
              double &xsnr,QString hiscall12,int *it);

    void baseline(double *s,int nfa,int nfb,double *sbase);
    bool first_ft8sbl;
    double window_ft8sbl[3890];    //NFFT1=2*NSPS NSPS=1920 1920*2=3840
    void get_spectrum_baseline(double *dd,int nfa,int nfb,double *sbase);
    void sync8(double *dd,double nfa,double nfb,double syncmin,double nfqso,double s_[402][1970],double candidate[2][620],int &ncand,double *sbase);
    void ft8apset(QString mycall12,QString hiscall12,int *apsym2);//int &iaptype ,QString hisgrid6,bool bcontest,QString mygrid6,

    void PrintMsg(QString,int,double,double,QString,int,float,float,bool &,bool);
    QString nutc0;
    int c_zerop;
    QString msg0[2][2][MAXDEC+20];
    double dt0[2][2][MAXDEC+20];
    double f0[2][2][MAXDEC+20];
    int ndec[2][2];
    int jseq;
    bool isgrid4(QString);
    int ft8_even_odd(QString);
    void ft8_a7_save(QString,double,double,QString);
    void ft8_a7d(double *dd0,bool &newdat,QString call_1,QString call_2,QString grid4,
                 double &xdt,double &f1,double xbase,int &nharderrors,double &,QString &msg37,double &xsnr);

};

//#include "../HvMsPlayer/libsound/HvGenFt4/gen_ft4.h"
class DecoderFt4
{

    int outCount;

public:
    explicit DecoderFt4(int id, std::shared_ptr<F2a> f2a);
    ~DecoderFt4();
    void SetStTxFreq(double f);
    void SetStMultiAnswerMod(bool f);
    void SetStDecoderDeep(int d);
    void SetStApDecode(bool f);// only in mshv
    void SetStQSOProgress(int i);
    void SetStDecode(QString time,int mousebutton);
    void SetStWords(QString,QString,int,int);
    void SetStHisCall(QString c);
    void SetMAMCalls(QStringList ls);
    //void SetNewP(bool);
    //void SetResetPrevT(QString ptime);
    void ft4_decode(double *dd,double f0a,double f0b,double,double,double fqso,bool &f);
    void SetResultsCallback(std::function<void(const char *)> fun) {
        this->resultsCallback = fun;
    }


    //signals:
    void EmitDecodedTextFt(QStringList lst) {
        char buf[1000] ="";
        snprintf(buf+strlen(buf), sizeof(buf)-strlen(buf), "FT4_OUT\t%lld\t%02d", currentTimeMillis(), outCount++);
        for(int i=0; i<lst.count(); i++) {
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "\t{%d}\t%s", i, lst[i].str->c_str());
            //        std::cout << "{" << i << "}" << lst[i].str->c_str() << " ";
        }
        strcat(buf,"\n");
        // fwrite(buf, 1, strlen(buf), stdout);
        // fflush(stdout);
        decodeResultOutput(buf);
        if (resultsCallback) {
            resultsCallback(buf);
        }
    }
    void EmitBackColor() {
        //
    }

private:
    int decid;
    std::shared_ptr<F2a> f2a;
    PomAll pomAll;
    PomFt pomFt;
    GenFt4 *TGenFt4;
    double DEC_SAMPLE_RATE;
    double twopi;
    double pi;

    //bool f_new_p;
    void dshift1(double *a,int cou_a,int ish);//???

    bool first_ft4_ds;
    std::complex<double> cx_ft4_ds[80000];     //(0:NMAX/2)=36288   31104                 [NMAX] (NMAX=21*3456)=72576
    double window_ft4_ds[4096]; //(0:NFFT2-1) (NFFT2=NMAX/18)=4032     (0:NFFT2-1) (NFFT2=NMAX/16)=3888
    void ft4_downsample(double *dd,bool newdata,double f0,std::complex<double> *c);

    //void nuttal_window(double *win,int n);
    void ft4_baseline(double *s,int nfa,int nfb,double *sbase);

    bool first_ft4detcad;
    double window_ft4[2314];    //2304;//NFFT1=2048;
    void getcandidates4(double *dd,double fa,double fb,double,double,double syncmin,double nfqso,
                        int maxcand,double candidate[2][115],int &ncand);

    bool first_ft4_sync4d;
    std::complex<double> csynca_ft4_sync[70];//(2*NSS) 2*32=64
    std::complex<double> csyncb_ft4_sync[70];
    std::complex<double> csyncc_ft4_sync[70];
    std::complex<double> csyncd_ft4_sync[70];
    void sync4d(std::complex<double> *cd0,int i0,std::complex<double> *ctwk,int itwk,double &sync);

    double pulse_ft4_rx[1748];          //576*3=1728    !512*3=1536
    void gen_ft4cwaveRx(int *i4tone,double f_tx,std::complex<double> *cwave);

    bool first_subsft4;
    std::complex<double> cw_subsft4[72800];//72576   =62208
    void subtractft4(double *dd,int *itone,double f0,double dt);

    int count_eq_bits(bool *a,int b_a,bool *b,int c);

    bool first_ft4bm;
    bool one_ft4_2[8][256];//(0:255,0:7)
    void get_ft4_bitmetrics(std::complex<double> *cd,double bitmetrics_[3][220],bool &badsync);//2*NN=206

    bool first_ft4d;
    int mrrr_ft4[19];
    int m73_ft4[19];
    int mrr73_ft4[19];
    int mcq_ft4[29];
    /*int mcqru_ft4[29];
    int mcqfd_ft4[29];
    int mcqtest_ft4[29];
    int mcqww_ft4[29];
    int mcqbu_ft4[29];
    int mcqft_ft4[29];*/
    int cont_id0_ft4_2;
    QString mycall0_ft4;
    QString hiscall0_ft4;
    double fac_ft4_sync;
    std::complex<double> ctwk2_ft4_[41][70]; //ctwk2(2*NSS,-16:16) 2*32=64
    int apbits_ft4[174];//174
    int apmy_ru_ft4[28];
    int aphis_fd_ft4[28];

    std::function<void(const char *)> resultsCallback;
};

class DecoderMs
{
public:
    DecoderMs();
    ~DecoderMs();

    void setMode(int mode);
    void SetDecoderDeep(int depth);
    void SetThrLevel(int threads);
    void SetWords(QStringList words,int contestCq,int contestType);
    void SetCalsHash(QStringList calls);
    void SetResultsCallback(std::function<void(const char *)> callback);
    void SetDecode(short *samples,int count,QString time,int start,int mouseButton,bool realtime,bool endRealtime,bool fileOpen);
    [[nodiscard]] bool IsWorking() const;

private:
    static constexpr int MAX_WORKERS = 6;
    static constexpr int MODE_FT8 = 11;
    static constexpr int MODE_FT4 = 13;

    DecoderFt8& ft8Decoder(int worker);
    DecoderFt4& ft4Decoder(int worker);
    int effectiveWorkerCount() const;
    void configure(DecoderFt8& decoder);
    void configure(DecoderFt4& decoder);

    std::shared_ptr<F2a> f2a;
    std::array<std::unique_ptr<DecoderFt8>,MAX_WORKERS> ft8Decoders;
    std::array<std::unique_ptr<DecoderFt4>,MAX_WORKERS> ft4Decoders;
    std::function<void(const char *)> resultsCallback;
    std::atomic_bool working = false;

    int mode = MODE_FT8;
    int workerCount = 1;
    int decoderDepth = 1;
    int contestCq = 0;
    int contestType = 0;
    QString myCall = "NOT__EXIST";
    QString myBaseCall = "NOT__EXIST";
    QString hisCall = "NOCALL";
    double receiveFrequency = 1270.46;
    double lowFrequency = 200.0;
    double highFrequency = 3200.0;
};

#endif
