#ifndef RANSAC_RUNNER
#define RANSAC_RUNNER

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/base.hpp>

class RansacRunner
{
public:
	// ��ʼ��
	RansacRunner(const float tolerance,
		const int num_sample,
		const int num_sub_sample,
		const int num_iter=2000);

	// ȡ�����Ӽ�: 3��������
	int GetMinSubSets(const std::vector<cv::Point3f>& Pts3D);

	// ȡ�����Ӽ�: ����3��������
	int GetSubSets(const std::vector<cv::Point3f>& Pts3D);

	// ����outlier ratio�����ܵĵ�������
	int UpdateNumIters(double p, double ep, int modelPoints, int maxIters);

	// 3���ռ��(�ǹ���)ȷ��һ���ռ�ƽ��
	int PlaneFitBy3Pts(const cv::Point3f* pts, float* plane_arr);

	// 3Dƽ�淽�����(��С���˷�)д��Ax=B����ʽ: aX + bY + Z + c = 0(aX + bY + c = -Z)
	int PlaneFitOLS(float* plane_arr);

	// ͳ���ڵ�(inlier)����
	int CountInliers(const float* plane_arr, const std::vector<cv::Point3f>& Pts3D);

	// ����RANSAC
	int RunRansac(const std::vector<cv::Point3f>& Pts3D);

	~RansacRunner();

//private:
	float m_tolerance;
	int m_num_sample;
	int m_num_sub_sample;
	int m_num_iter;
	int m_num_inliers;

	float m_plane[4];
	cv::Point3f m_min_subsets[3];
	std::vector<cv::Point3f> m_subsets;
};

#endif // !1