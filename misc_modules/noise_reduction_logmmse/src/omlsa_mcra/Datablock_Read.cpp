#define _CRT_SECURE_NO_WARNINGS
#include <math.h>
#include "Datablock_Read.h"
#include "head.h"
using namespace std;

Datablock_Read::Datablock_Read(int sample_rate,short channels,int MaxDataLen)
{
	m_channels = channels;
	m_sample_rate = sample_rate;
	m_derr_code=Initial(MaxDataLen);
}

short Datablock_Read::Initial(int MaxDataLen) {
	m_maxdata = MaxDataLen;
	if (m_sample_rate < 23000)	//������С��23K
	{
		m_wlen = 256;
		m_inc = 128;  //֡��Ϊ4�Ĵη�
	}
	else {
		m_wlen = 1024;
		m_inc = 512;
	}
	m_wlen15 = m_wlen +(m_wlen>>1);
	m_inc2 = m_inc <<1;

	m_inc_move = log(m_inc) / log(2);

	m_DoubDataBuffer.resize(MaxDataLen + m_wlen);
	m_data_in.resize(m_wlen15);
	m_data_storage.resize(m_wlen15);
	m_process_storage.resize(m_wlen15);
	m_data_out.resize(m_wlen15);
	m_data_resize.resize(MaxDataLen + m_wlen);
	m_data_rest_length = 0;
	m_blockInd = 0;
	short m_ierr_code = LSA.Initialize(m_wlen15); //15--49
	if (m_ierr_code < 0)return m_ierr_code;
	//My_fft->initial(m_wlen); //	FFT��ʼ��  ����ת��
	return 0;
}

short Datablock_Read::Data_procese(short* pInBuffer, short* pOutBuffer,int read_length,int& Out_Length){
	if (m_derr_code < 0) return m_derr_code;
	if (pInBuffer == NULL)return -50;
	if (pOutBuffer == NULL)return -51;

	if (m_channels == 2)
		read_length = (read_length >> 1);
	int current_total_data = read_length + m_data_rest_length;  // ���������ݸ���

	if (current_total_data < m_wlen15) {	//  ���ݲ���һ֡
		if(m_channels==1)
		memcpy(m_data_storage.data() + m_data_rest_length, pInBuffer, (read_length) * sizeof(short));
		else if (m_channels == 2) {
			for (int i = 0; i < read_length; i++) 
				m_data_storage[i + m_data_rest_length] = (pInBuffer[i << 1] + pInBuffer[(i << 1) + 1]) >> 1;
		}

		Out_Length = 0;
		m_data_rest_length = current_total_data;
	}
	else {
	// ��������  ���ǵ�һ֡���ݵĴ洢
	// �Ƚ����������ݵ���
	memcpy(m_data_resize.data(), m_data_storage.data(), (m_data_rest_length) * sizeof(short));
	// �ٽ���ȡ���ݵ���
	if (m_channels == 1){
		memcpy(m_data_resize.data() + m_data_rest_length, pInBuffer, read_length * sizeof(short));
	}
	else if (m_channels == 2) {
		for (int i = 0; i < read_length; i++) {
			m_data_resize[i + m_data_rest_length] = (pInBuffer[i << 1] + pInBuffer[(i << 1) + 1]) >> 1;   //2^15
		}
	}

 	int data_use = 0;				 // �����ȡ���������Ѿ�ʹ�ù�������-��֡ͷ����
	int current_frame = ((current_total_data - m_wlen) >> m_inc_move) + 1;  // ��ǰ�������ܹ��ɵ�֡��
	//�Զ���֡��ȡż��  �������ִ���ݴ���
	current_frame = (current_frame | 1) - 1;

	m_data_rest_length = current_total_data -( current_frame*m_inc);  // ����ʣ������ β֡��֮��
	Out_Length = current_frame * m_inc;	  // �����ĩ����

	for (int k = 0; k < current_frame-1; k+=2, m_blockInd+=2){
		memcpy(m_data_in.data(), m_data_resize.data() + data_use, m_wlen15 * sizeof(short)); //��֡
		memset(m_data_out.data(), 0, sizeof(short)*m_wlen15);
		m_derr_code=LSA.Denoise_process(m_data_in.data(), m_data_out.data(), m_blockInd);

		if (m_derr_code < 0) return m_derr_code;
		//���Ҫ���ص��õ���֡
 		int block_inc = k * m_inc;  //�εĵ�ǰλ��
		if (current_frame == 2) {   
			for (int i = 0; i < m_inc; i++) {
				m_DoubDataBuffer[block_inc + i] += m_data_out[i];
				m_DoubDataBuffer[block_inc + i] += m_process_storage[i];
				m_DoubDataBuffer[block_inc + m_inc+i] += m_data_out[i+m_inc];
				m_process_storage[i] = m_data_out[i+m_wlen];
			}
		}
		else {
			if (k == 0) {                    
				for (int i = 0; i < m_inc; i++) {
					m_DoubDataBuffer[block_inc + i] += m_data_out[i];
					m_DoubDataBuffer[block_inc + i] += m_process_storage[i];
					m_DoubDataBuffer[block_inc + m_inc + i] += m_data_out[i + m_inc];
					m_DoubDataBuffer[block_inc + m_wlen + i ] += m_data_out[i + m_wlen];
				}
			}
			else if (k == current_frame-2 ){
				for (int i = 0; i < m_inc; i++) {   
					m_DoubDataBuffer[block_inc + i] += m_data_out[i];
					m_DoubDataBuffer[block_inc + m_inc + i] += m_data_out[i + m_inc];
					m_process_storage[i] = m_data_out[i + m_wlen];
				}
			}
			else {   //���м�֡
				for (int i = 0; i < m_wlen15; i++) {
					m_DoubDataBuffer[block_inc + i] += m_data_out[i];
				}
			}
		}
		data_use += m_wlen;
	}
	if (m_channels == 1) {
		memcpy(pOutBuffer, m_DoubDataBuffer.data(), Out_Length * sizeof(short));
	}
	else if (m_channels == 2){
		for (int i = 0; i < Out_Length; i++) {
			pOutBuffer[i << 1] = m_DoubDataBuffer[i];
			pOutBuffer[(i << 1) + 1] = m_DoubDataBuffer[i];  //˫ͨ����� 
		}
	}
	if (m_channels == 2)
		Out_Length = Out_Length << 1;  
	//�����������   
	memcpy(m_data_storage.data(), m_data_resize.data() + data_use, (m_data_rest_length) * sizeof(short));
	std::fill_n(m_DoubDataBuffer.begin(), current_frame * m_inc, 0);
	}
	return 0;
}



