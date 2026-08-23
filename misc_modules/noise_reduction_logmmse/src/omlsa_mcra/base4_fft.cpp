#include "base4_fft.h"
#include <math.h>
#include<iostream>

MY_B4_FFT::MY_B4_FFT() {
}

short MY_B4_FFT::initial(int N) {
	m_fwlen = N;  //��ʵ֡���ĳ�ʼ��
	m_finc = m_fwlen>>1;
	m_M4 = 0;
	int N_pow = 1;
	while (N_pow < m_fwlen && N_pow <= 16384 / 4) {
		N_pow <<= 2;
		m_M4++;
	}
	if (N_pow != m_fwlen || m_fwlen <= 0 || m_fwlen > 16384) {  //2^14 =16384   2^12=4096
		return -1;
	}  
	m_Buffer_cos.resize(m_fwlen);
	m_Buffer_sin.resize(m_fwlen);
	m_sort4_count.resize(m_fwlen);
	m_sort_temp_r.resize(m_fwlen);
	m_sort_temp_i.resize(m_fwlen);
	m_e1_position.resize(m_fwlen);
	m_o1_position.resize(m_fwlen);
	m_e2_position.resize(m_fwlen);
	m_o2_position.resize(m_fwlen);
	m_yr.resize(m_fwlen);
	m_yi.resize(m_fwlen);
	m_ifft_move_bit = m_M4<<1;  // �ȼ���  /m_fwlen

	for (int i = 0; i < m_fwlen; i++) {  
		m_Buffer_cos[i] = (coc.cordic_cos((int)(-2 * Pi*i * 32768)>>m_ifft_move_bit));
		m_Buffer_sin[i] = (coc.cordic_sin((int)(-2 * Pi*i * 32768)>>m_ifft_move_bit));
	}
	Base4_Sort();
	return 0;
}

void MY_B4_FFT::Base4_Sort(){
	int  bit_rest, space, add;
	std::fill(m_sort4_count.begin(), m_sort4_count.end(), 0);
	for (int l = 0; l < m_M4; l++)
	{
		space = 1 << (2 * l);
		add = 1 << (2 * (m_M4 - l - 1));
		for (int i = 0; i < m_fwlen; i++){
			bit_rest = (i / space) % 4;
			if (bit_rest != 0)			   
				m_sort4_count[i] += add * bit_rest;// λ������ڳ��������Ķ�Ӧλ
		}
	}
}

short MY_B4_FFT::base4_fft(Complex_num *x, int sign) {
	int i, j, k, tr, ti, wi2, wi3;
	if (x == NULL) {
		return -13;//
	}
	if (!abs(sign)) {
		return -14;//
	}
	//���������������н��е�λ��任
	int ctemp;
	for (int i = 0; i < m_fwlen; i++){
		m_sort_temp_r[i] = x[i].real;
		m_sort_temp_i[i] = x[i].imag;
	}
	for (int i = 0; i < m_fwlen; i++){
		ctemp = m_sort4_count[i];
		x[i].real = m_sort_temp_r[ctemp];
		x[i].imag = m_sort_temp_i[ctemp];
	}

	int BlockLen, BlockNum, BlockLen2;  int wi, F1, F2; int F3, F0; Complex_num X0, X1, X2, X3;
	for (i = 1; i <= m_M4; i++) {
		BlockNum = 1 << (2 * (m_M4 - i));
		BlockLen = 1 << (2 * i);		// �����+�������ݸ���  interval_2
		BlockLen2 = BlockLen >> 2;	// ���ڵ��θ���  interval_1
		for (j = 0; j < BlockNum; j++) {	  // ��ÿһ����Ļ�4����ѭ������
			for (k = 0; k < BlockLen2; k++) { // Ϊ���ڵĵ�k������
				F0 = k + j * BlockLen;		  // ÿһ���εĵ�һ������
				F1 = F0 + BlockLen2;
				F2 = F1 + BlockLen2;
				F3 = F2 + BlockLen2;
				wi = BlockNum * k;			  // ��ת���ӵ�ָ��
				wi2 = wi << 1;
				wi3 = (wi << 1) + wi;

				X0.real = x[F0].real;
				X0.imag = x[F0].imag;
				X1.real = (((__int64)x[F1].real*m_Buffer_cos[wi]) >> 30) - (((__int64)x[F1].imag*m_Buffer_sin[wi]) >> 30);
				X1.imag = (((__int64)x[F1].imag*m_Buffer_cos[wi]) >> 30) + (((__int64)x[F1].real*m_Buffer_sin[wi]) >> 30);
				X2.real = (((__int64)x[F2].real*m_Buffer_cos[wi2]) >> 30) - (((__int64)x[F2].imag*m_Buffer_sin[wi2]) >> 30);
				X2.imag = (((__int64)x[F2].imag*m_Buffer_cos[wi2]) >> 30) + (((__int64)x[F2].real*m_Buffer_sin[wi2]) >> 30);
				X3.real = (((__int64)x[F3].real*m_Buffer_cos[wi3]) >> 30) - (((__int64)x[F3].imag*m_Buffer_sin[wi3]) >> 30);
				X3.imag = (((__int64)x[F3].imag*m_Buffer_cos[wi3]) >> 30) + (((__int64)x[F3].real*m_Buffer_sin[wi3]) >> 30);

				x[F0].real = X0.real + X1.real + X2.real + X3.real;
				x[F0].imag = X0.imag + X1.imag + X2.imag + X3.imag;
				x[F1].real = X0.real + X1.imag - X2.real - X3.imag;
				x[F1].imag = X0.imag - X1.real - X2.imag + X3.real;
				x[F2].real = X0.real - X1.real + X2.real - X3.real;
				x[F2].imag = X0.imag - X1.imag + X2.imag - X3.imag;
				x[F3].real = X0.real - X1.imag - X2.real + X3.imag;
				x[F3].imag = X0.imag + X1.real - X2.imag - X3.real;
			}
		}
	}

	if (sign == -1)
	{
		x[0].real = x[0].real >> m_ifft_move_bit;
		x[0].imag = x[0].imag >> m_ifft_move_bit;
		for (i = 1; i <= m_finc; i++) {
			m_fly_tempr = (x[m_fwlen - i].real) >> m_ifft_move_bit;
			m_fly_tempi = (x[m_fwlen - i].imag) >> m_ifft_move_bit;
			x[m_fwlen - i].real = (x[i].real) >> m_ifft_move_bit;
			x[m_fwlen - i].imag = (x[i].imag) >> m_ifft_move_bit;
			x[i].real = m_fly_tempr;
			x[i].imag = m_fly_tempi;
		}
	}
	else if (sign == 1) {  
		//������ʵ���и���Ҷ�任�Ļ�ԭ  
		for (int i = 0; i < m_fwlen; i++) {
			if (i == 0) {
				m_yr[i] = x[i].real; 
				m_yi[i] = x[i].imag;
			}
			else {
				m_yr[i] = x[m_fwlen - i].real; 
				m_yi[i] = x[m_fwlen - i].imag;
			}
			m_e1_position[i] = (x[i].real + m_yr[i]) >> 1;
			m_o1_position[i] = (x[i].real - m_yr[i]) >> 1;
			m_e2_position[i] = (x[i].imag + m_yi[i]) >> 1;
			m_o2_position[i] = (x[i].imag - m_yi[i]) >> 1;
		}
		for (int i = 0; i < m_fwlen; i++) {
			x[i].real = m_e1_position[i];
			x[i].imag = m_o2_position[i];
			x[i + m_fwlen].real = m_e2_position[i];
		 	x[i + m_fwlen].imag = -m_o1_position[i];
		}
	}
	return 0;
}

void MY_B4_FFT::fftfix(Complex_num *x, int sign) {

	//--------------------------------------------------------------------------
	//��ʱ���ȡ����fft�任�����뷴���������

	int i, j, k, u = 0, l = 0, wi = 0, n1, tr, ti, N = m_fwlen; //j�ڶ���ѭ�����ӿ��е�ÿ�����ε�ѭ��������
									 //k��һ��ѭ��������fft�任������Ϊlog2��N��NΪ�ܲ�������
									 //u �����ϱ�x[upper],l �����±�x[lower]��wi��ת�����±�wn[wi]
	int SubBlockNum, SubBlockStep = 1;
	//SubBlockNum��ǰk���ӿ�������SubBlockStep��ǰk�㲻ͬ�ӿ����ͬλ��Ԫ�ؼ���

	int tempr, tempi;
	N = N << 1;

	n1 = m_fwlen - 1;
	for (j = 0, i = 0; i < n1; i++)
	{              // λ��ת����
		if (i < j)
		{
			tr = x[i].real;
			ti = x[i].imag;

			x[i].real = x[j].real;
			x[i].imag = x[j].imag;

			x[j].real = tr;
			x[j].imag = ti;
		}
		k = N / 2;
		while (k < (j + 1))
		{
			j = j - k;
			k = k / 2;
		}
		j = j + k;
	}

	for (k = N; k > 1; k = (k >> 1)) {				//��һ��ѭ��������log2(k)�׵ı任

		SubBlockNum = k >> 1;				//�ӿ����Ϊ����������һ��
		SubBlockStep = SubBlockStep << 1;	//�ӿ��ͬ�ȵ�λ��Ԫ�ؼ����2Ϊ��������
		wi = 0;							//��ת���ӳ�ʼ��
		for (j = 0; j < SubBlockStep >> 1; j++) {	//�ڶ���ѭ��������jֵ��j��ʾ�����ӿ�ĵ�j�����Ρ�
			//��Ϊÿ���ӿ��ͬ��λ���ξ�����ͬ��wn�������õڶ���ѭ������wn
			for (u = j; u < N; u += SubBlockStep) {	//������ѭ����ѭ���ڸ����ӿ��ĵ�j�����Σ��������е��Ρ�
				//ֱ���±�uԽ�硣(u>N)		
				l = u + (SubBlockStep >> 1);//�±�l����

				//ͬʱ������ʵ���е�FFT  cos-isin
				tempr = (((__int64)x[l].real*m_Buffer_cos[wi]) >> 30) - (((__int64)x[l].imag*m_Buffer_sin[wi]) >> 30);
				//����x[u]=x[u]+x[l]*Wn,x[l]=x[u]-x[l]*Wn�ĸ�������
				tempi = (((__int64)x[l].imag*m_Buffer_cos[wi]) >> 30) + (((__int64)x[l].real*m_Buffer_sin[wi]) >> 30);

				x[l].real = x[u].real - tempr;  //������ÿ�θ���һ����ת���Ӵ����ı仯
				x[l].imag = x[u].imag - tempi;
				x[u].real = x[u].real + tempr;
				x[u].imag = x[u].imag + tempi;
			}
			wi += SubBlockNum;	//�ڶ���ѭ������wiֵ
		}
	}

	if (sign == -1)
	{
		for (i = 0; i < N; i++)
		{
			x[i].real /= N;
			x[i].imag /= N;
		}
	}
}
