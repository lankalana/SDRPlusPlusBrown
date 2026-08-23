#include "G_calculate.h"
#include<iostream>
#include"SearchChart.h"
#include"time.h"
#include<algorithm>
#include<cstdint>
#include <sdrpp_export.h>

using namespace std;

#ifdef _WIN32
#define _WINSOCKAPI_    // stops windows.h including winsock.h
#endif
SDRPP_EXPORT char* sdrppResourcesDirectory;

G_calculate::G_calculate()
{
}
short G_calculate::Initialize(int wlen) {
	m_lwlen = wlen ;
	m_linc = m_lwlen >> 1;
	m_linc_move = log(m_linc) / log(2);
	N_wlen1 = m_lwlen + 1;
	m_num_mag = 128;       // 2^7
	m_num_mag_pow = 16384; // 2^14
	m_num_mag_pow2 = 268435456; //2^28
	m_amp_para = 7, m_amp_para_double = 14, m_amp_para_three = 21;  
	m_snpramin = 0.03*m_num_mag_pow;
	beta = 0.7*m_num_mag_pow;
	m_cosen_max = 0.8*m_num_mag_pow;  // %:0.1 : 10
	m_cosen_min = 0.1*m_num_mag_pow;  // :0.1:10  
	m_cosen_pmax = 5 * m_num_mag_pow;			// %ʵ��֤��Ҫ����1.3����ȷ�Ͻ�
	m_cosen_pmin = 1 * m_num_mag_pow;			// %1.0~1.1֮�����
	m_cosen_max_min = log(8) *m_num_mag_pow;
	m_log_cosen_min = log(m_cosen_min);
	m_log_cosen_ratio = log(static_cast<double>(m_cosen_max / m_cosen_min));
	m_w_global = 8; m_wt_global = 17;

	const size_t spectrumSize = static_cast<size_t>(N_wlen1);
	m_arr_temp.resize(spectrumSize);
	m_abs_Y.resize(spectrumSize);
	m_init_S.resize(spectrumSize);
	m_Gh1.resize(spectrumSize);
	m_q.resize(spectrumSize);
	m_pp.resize(spectrumSize);
	m_G.resize(spectrumSize);
	m_M.resize(spectrumSize);
	m_cosen_local.resize(spectrumSize);
	m_cosen_global.resize(spectrumSize);
	m_plocal.resize(spectrumSize);
	m_pglobal.resize(spectrumSize);
	m_h_global.resize(m_wt_global);
	m_old_cosen.resize(spectrumSize);
	m_cosen.resize(spectrumSize);
	m_lamda_d.resize(spectrumSize);
	m_integra.resize(spectrumSize);
	m_init_S_min.resize(spectrumSize);
	m_init_S_tmp.resize(spectrumSize);
	m_init_p1.resize(spectrumSize);
	m_EN_cos.resize(spectrumSize);
	m_EN_sin.resize(spectrumSize);
	m_post_SNR.resize(spectrumSize);
	m_pr_SNR.resize(spectrumSize);
	m_E_pr_SNR.resize(spectrumSize);
	m_v.resize(spectrumSize);
	
	for (int i = 1; i < (m_wt_global + 1); ++i)  //2^28=268435456 
		m_h_global[i - 1] = (0.5 - 0.5 * cos(2.0 * Pi*(i) / (m_wt_global + 1))) * 16384;//2^14=16384

	for (int i = 0; i < m_lwlen; i++) 
		m_pr_SNR[i] = 0.98*m_num_mag_pow;

	std::fill(m_old_cosen.begin(), m_old_cosen.end(), 0);
	std::fill(m_cosen.begin(), m_cosen.end(), 0);

	//��new���Ŀռ䷵��
	//const char* Fileexpint = "D:/exppow3.pcm";
	//m_int_value = file_read<int>(Fileexpint);  
   //const char* Fileexpsub = "D:/expsub1.pcm";
   //m_expsub_value = file_read<int>(Fileexpsub);



//	const char* FileexpG = "/home/san/Fun/OMLSA-MCRA/OM_LSA/Gvalue2.pcm";
    std::string full = std::string(sdrppResourcesDirectory) + "/cty/oomlsa_gcra_gvalue2.pcm";
#ifdef _WIN32
    std::replace(full.begin(), full.end(), '/', '\\');
    std::string::size_type pos = 0;
    while ((pos = full.find("\\\\", pos)) != std::string::npos) {
        full.replace(pos, 2, "\\");
        pos += 1; // Move to the next position after the replaced slash
    }
#endif

	m_G_value = loadGainTable(full.c_str());
	if (!m_G_value) {
		return -29;
	}
	//cout << sizeof(m_G_value) << sizeof(m_G_value) / sizeof(m_G_value[0]);
	return 0;
}

void G_calculate::NoiseEstimation(int blockInd)
{
	int p;
	int L;
	//����ǰ10֡���ݣ�����L�Լӿ�������ĸ����ٶ�
	if (blockInd >= 10) {
		L = 50;
	}
	else {
		L = 1;
		if (blockInd == 0) {
			for (int i = 0; i <= m_linc; i++) {
				m_init_S[i] = m_abs_Y[i];  //��ȡ��ʼ��ֵ���ʼ��
				m_init_S_min[i] = m_init_S[i];
				m_init_S_tmp[i] = m_init_S[i];
				m_lamda_d[i] = m_init_S[i];
			}
			std::fill_n(m_init_p1.begin(), m_linc + 1, 0);
		}
	}

	if ((blockInd + 1) % L == 0) {
		for (int k = 0; k <= m_linc; k++) {
			m_init_S_min[k] = min(m_init_S_tmp[k], m_init_S[k]);
			m_init_S_tmp[k] = m_init_S[k];
		}
	}

	for (int k = 0; k <= m_linc; k++)
	{
		m_init_S[k] = (m_init_S[k] >> 1) + (m_init_S[k] >> 2) + (m_init_S[k] >> 4) + (m_abs_Y[k] >> 3) + (m_abs_Y[k] >> 4);
		m_init_S_min[k] = min(m_init_S_min[k], m_init_S[k]);   // ����L�����˾ֲ���С�����ķֱ���
		m_init_S_tmp[k] = min(m_init_S_tmp[k], m_init_S[k]);

		if (m_init_S[k] > (m_init_S_min[k] >> 2) + (m_init_S_min[k] << 1))
			p = m_num_mag_pow;
		else
			p = 0;

		m_init_p1[k] = (m_init_p1[k] >> 2) + (p >> 1) + (p >> 2);
		m_lamda_d[k] = ((__int64)((m_lamda_d[k] >> 1) + (m_lamda_d[k] >> 2) + (m_lamda_d[k] >> 3) + (m_lamda_d[k] >> 4) + (m_abs_Y[k] >> 4))
			*(m_num_mag_pow - m_init_p1[k]) + (__int64)m_lamda_d[k] * m_init_p1[k]) >> m_amp_para_double;
		m_lamda_d[m_lwlen - k] = m_lamda_d[k];
	}
}
void G_calculate::SpeechAbsenceEstm()
{
	unsigned long long sum = 0, a = 0;
	int p_frame, mu, cosen_peak;
	short old_cosen_frame = 0, cosen_frame = 0;

	for (int k = 0; k <= (m_linc + m_w_global); k++) {   //����ļ�����õ�
		m_cosen[k] = (m_old_cosen[k] >> 1) + (m_old_cosen[k] >> 2) + (m_E_pr_SNR[k] >> 2);
	}

	for (int k = 0; k <= m_linc; k++) {
		if (k <= m_w_global - 1) {  // ��һ֡������ͷȥβ
			m_cosen_global[k] = m_cosen[k];
			if (k == 0)
				m_cosen_local[k] = m_cosen[k];
			else
				m_cosen_local[k] = (m_cosen[k - 1] + (m_cosen[k] << 1) + m_cosen[k + 1]) >> 2;
		}
		else {
			a = 0;
			for (int j = 0; j < 2 * m_w_global + 1; j++)
				a = a + ((__int64)m_h_global[j] * m_cosen[k + m_w_global - j]);
			m_cosen_global[k] = ((a >> m_amp_para_double) / (m_w_global + 1));
			m_cosen_local[k] = (m_cosen[k - 1] + (m_cosen[k] << 1) + m_cosen[k + 1]) >> 2;
		}

		if (m_cosen_local[k] <= m_cosen_min)   //% (25)
			m_plocal[k] = 0;
		else if (m_cosen_local[k] >= m_cosen_max)
			m_plocal[k] = m_num_mag_pow; //��ȷ�Ŵ�10000��
		else
			m_plocal[k] = m_num_mag_pow2 * (log(m_cosen_local[k]) - m_log_cosen_min) / m_cosen_max_min;

		if (m_cosen_global[k] <= m_cosen_min)   //% (25)
			m_pglobal[k] = 0;
		else if (m_cosen_global[k] >= m_cosen_max)
			m_pglobal[k] = m_num_mag_pow;
		else
			m_pglobal[k] = m_num_mag_pow2 * (log(m_cosen_global[k]) - m_log_cosen_min) / m_cosen_max_min;
		sum += m_cosen[k];
	}

	cosen_frame = sum >> m_linc_move; //10000
	sum = 0;
	cosen_peak = std::min<short>(std::max<short>(cosen_frame, m_cosen_pmin), m_cosen_pmax);//10000

	if (cosen_frame <= ((cosen_peak * m_cosen_min) >> m_amp_para_double))   // (27)
		mu = 0;
	else if (cosen_frame >= ((cosen_peak * m_cosen_max) >> m_amp_para_double))
		mu = m_num_mag_pow;
	else
		mu = m_num_mag_pow * log(cosen_frame  * m_num_mag_pow / cosen_peak / m_cosen_min) / m_log_cosen_ratio;

	if (cosen_frame > m_cosen_min)
	{
		if (cosen_frame > old_cosen_frame)
			p_frame = m_num_mag_pow;
		else
		{
			p_frame = mu;  //�����õ�  ����������ȼ��˶��ӵ����
		}
	}
	else
	{
		p_frame = 0;
	}
	for (int k = 0; k <= m_linc; k++)
	{
		m_q[k] = m_num_mag_pow - ((__int64)m_plocal[k] * m_pglobal[k] * p_frame >> 28);//10000
		m_q[k] = min<int>(m_q[k], 0.95*m_num_mag_pow);
		m_old_cosen[k] = m_cosen[k];
		m_old_cosen[m_lwlen - k] = m_old_cosen[k];
	}
}
short G_calculate::G_calculate_process(Complex_num* winData, int blockInd) {  // -49
	if (winData == NULL)return -49;

	int post_temp, w = 8;
	for (int i = 0; i <= m_linc + w; i++) {
		m_abs_Y[i] = static_cast<unsigned int>(std::hypot(static_cast<double>(winData[i].real), static_cast<double>(winData[i].imag)));  //m_abs_Y����2^6��
		m_EN_cos[i] = ((__int64)winData[i].real << 12) / (1 > m_abs_Y[i] ? 1 : m_abs_Y[i]);
		m_EN_sin[i] = ((__int64)winData[i].imag << 12) / (1 > m_abs_Y[i] ? 1 : m_abs_Y[i]);
	}
	NoiseEstimation(blockInd);
	for (int i = 0; i <= m_linc; i++) {
		constexpr std::int64_t maxPostSnr = 4096LL << 14;
		constexpr std::int64_t maxPosteriorRatio = 1LL << 20;
		static_assert((maxPosteriorRatio * maxPosteriorRatio >> 14) == maxPostSnr, "Posterior SNR clamp must preserve the existing saturation limit");
		const std::int64_t posteriorRatio = (static_cast<std::int64_t>(m_abs_Y[i]) * m_num_mag_pow) / (1 > m_lamda_d[i] ? 1 : m_lamda_d[i]);
		const std::int64_t boundedPosteriorRatio = (std::min)(posteriorRatio, maxPosteriorRatio);
		m_post_SNR[i] = static_cast<int>((boundedPosteriorRatio * boundedPosteriorRatio) >> m_amp_para_double);
		post_temp = max<int>(m_post_SNR[i] - m_num_mag_pow, 0);
		m_E_pr_SNR[i] = min<int>(max<int>((m_pr_SNR[i] >> 1) + (m_pr_SNR[i] >> 2) + (m_pr_SNR[i] >> 3) + (post_temp >> 3), m_snpramin), 4096 << m_amp_para_double);
		m_E_pr_SNR[m_lwlen - i] = m_E_pr_SNR[i];                                                // 0.0001 * (2^24=16777216) =1678  167772 
		m_v[i] = max<__int64>(min<__int64>((__int64)((__int64)m_E_pr_SNR[i] * m_post_SNR[i] << 10) / (m_num_mag_pow + m_E_pr_SNR[i]), 15 << 24), 1678);
		m_integra[i] = expintpow_solution(m_v[i]);  // ����ֵ�Ŵ�14��  =exp(expint(v)/2)
		m_Gh1[i] = min<int>((__int64)m_E_pr_SNR[i] * m_integra[i] / (m_num_mag_pow + m_E_pr_SNR[i]), 70 << 14);
	}
	SpeechAbsenceEstm();
	for (int i = 0; i <= m_linc; i++) {
		m_integra[i] = subexp_solution(m_v[i]);  // exp(-(double)m_v[i] / (1 << 24))
		m_arr_temp[i] = m_num_mag_pow + ((__int64)(m_num_mag_pow + m_E_pr_SNR[i]) * m_integra[i] * m_q[i] >> m_amp_para_double) / (m_num_mag_pow - m_q[i]);
		
		/*m_arr_temp[i] = (m_num_mag + ((int)((m_num_mag_pow + m_E_pr_SNR[i]) *m_q[i] * exp(-(double)m_v[i] / m_num_mag_pow)) >> 7)
			/ (m_num_mag_pow - m_q[i]));*/
		m_pp[i] = min(m_num_mag_pow2 / m_arr_temp[i], 1 << 14);
		m_G[i] = Gvalue_solution(m_Gh1[i], m_pp[i]); // Gh1^pp * 0.003^(1-pp)<<14
		//m_G[i] = pow((double)m_Gh1[i] / 16384, (double)m_pp[i] / 16384) * 16384* pow(0.003, (1 - (double)m_pp[i] / 16384));  //��16��14

		m_M[i] = ((__int64)m_G[i] * m_abs_Y[i]) >> m_amp_para_double;  //��ֵ
		winData[i].real = ((__int64)m_M[i] * m_EN_cos[i]) >> 12; 
		winData[i].imag = ((__int64)m_M[i] * m_EN_sin[i]) >> 12;
		constexpr std::int64_t maxPriorSnr = 4096LL << 14;
		constexpr std::int64_t maxPriorRatio = 1LL << 13;
		static_assert(maxPriorRatio * maxPriorRatio == maxPriorSnr, "Prior SNR clamp must preserve the existing saturation limit");
		const std::int64_t priorRatio = static_cast<std::int64_t>(m_M[i]) * m_num_mag / (1 > m_lamda_d[i] ? 1 : m_lamda_d[i]);
		const std::int64_t boundedPriorRatio = (std::min)(priorRatio, maxPriorRatio);
		m_pr_SNR[i] = static_cast<int>(boundedPriorRatio * boundedPriorRatio);  // 10000
		winData[m_lwlen - i].real = winData[i].real;
		winData[m_lwlen - i].imag = -winData[i].imag;
	}
	
	return 0;
}


