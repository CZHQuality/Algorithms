#ifndef COLMAP_SRC_MVS_WORKSPACE_H_
#define COLMAP_SRC_MVS_WORKSPACE_H_

#include "consistency_graph.h"
#include "depth_map.h"
#include "model.h"
#include "normal_map.h"

#include "SLICSuperpixels.h"


namespace colmap {
	namespace mvs {

		class Workspace {
		public:
			struct Options {
				// The maximum cache size in gigabytes.
				double cache_size = 32.0;

				// Maximum image size in either dimension.
				int max_image_size = 400;  // -1

				// Whether to read image as RGB or gray scale.
				bool image_as_rgb = true;

				////�Ƿ�����ϸ����ǿ��ͼ��
				bool bDetailEnhance = true;

				////�Ƿ����ýṹ��ǿ��ͼ��
				bool bStructureEnhance = false;

				////�Ƿ���н���������(��ͼ��߶Ⱥ�������������޸ģ�ϡ����άģ�Ͳ���)
				bool bDown_sampling = false;

				//�������ĳ߶�
				float fDown_scale = 4.0f;

				bool bOurs = false;  // ����˫�ߴ����ϲ���
				bool bOursFast = false;  // ��������˫�ߴ����ϲ�������

				bool bBilinearInterpolation = 0;  // ˫���Բ�ֵ�ϲ���

				bool bFastBilateralSolver = 0;  // ����˫�������

				bool bJointBilateralUpsampling = 0;  // ����˫���ϲ���

				bool bBilateralGuidedUpsampling = 0;  // ˫�������ϲ���

				// Location and type of workspace.
				std::string workspace_path;
				std::string workspace_format;
				std::string input_type;
				std::string input_type_geom;
				std::string newPath;
				std::string src_img_dir;

				//ȥŤ����Ŀ¼
				std::string undistorte_path;

				// �����طָ�Ŀ¼
				std::string slic_path;
			};

			// ���캯��: ��bundler�����ж�ȡϡ�������Ϣ
			Workspace(const Options& options);

			const Model& GetModel() const;
			const cv::Mat& GetBitmap(const int image_id);
			const DepthMap& GetDepthMap(const int image_id) const;
			const NormalMap& GetNormalMap(const int image_id) const;
			const ConsistencyGraph& GetConsistencyGraph(const int image_id) const;

			const void ReadDepthAndNormalMaps(const bool isGeometric);

			const std::vector<DepthMap>& GetAllDepthMaps() const;
			const std::vector<NormalMap>& GetAllNormalMaps() const;

			//��������Ľ����д��workspace��Ӧ�ı�������
			void WriteDepthMap(const int image_id, const DepthMap &depthmap);
			void WriteNormalMap(const int image_id, const NormalMap &normalmap);
			void WriteConsistencyGraph(const int image_id, const ConsistencyGraph &consistencyGraph);

			//ִ�г����طָ�
			void runSLIC(const std::string &path);

			//����ά��ͶӰ��ͼ����
			void showImgPointToSlicImage(const std::string &path);

			//�����ͼ�ͷ���ͼ�����ϲ�����ͬʱ�޸�model�е�ͼ����Ϣ
			void UpSampleMapAndModel();

			// Get paths to bitmap, depth map, normal map and consistency graph.
			std::string GetBitmapPath(const int image_id) const;
			std::string GetDepthMapPath(const int image_id, const bool isGeom) const;
			std::string GetNormalMapPath(const int image_id, const bool isGeom) const;
			std::string GetConsistencyGaphPath(const int image_id) const;

			// Return whether bitmap, depth map, normal map, and consistency graph exist.
			bool HasBitmap(const int image_id) const;
			bool HasDepthMap(const int image_id, const bool isGeom) const;
			bool HasNormalMap(const int image_id, const bool isGeom) const;

			float GetDepthRange(const int image_id, bool isMax) const;

			void jointBilateralUpsampling(const cv::Mat &joint, const cv::Mat &lowin, const float upscale,
				const double sigma_color, const double sigma_space, int radius, cv::Mat &highout) const;

			void jointBilateralPropagationUpsampling(const cv::Mat &joint, const cv::Mat &lowDepthMat, const cv::Mat &lowNormalMat, const float *refK,
				const float upscale, const double sigma_color, const double sigma_space, const int radius, cv::Mat &highDepthMat) const;

			void jointBilateralDepthMapFilter1(const cv::Mat &srcDepthMap, const cv::Mat &srcNormalMap, const cv::Mat &srcImage, const float *refK,
				const int radius, const double sigma_color, const double sigma_space, DepthMap &desDepMap, NormalMap &desNorMap, const bool DoNormal)const;

			float PropagateDepth(const float *refK, const float depth1, const float normal1[3],
				const float row1, const float col1, const float row2, const float col2) const;

			void SuitNormal(const int row, const int col, const float *refK, float normal[3]) const;

			//�Է�����ͼ��������ֵ�˲�
			void NormalMapMediaFilter(const cv::Mat &InNormalMapMat, cv::Mat &OutNormalMapMat, const int windowRadis)const;
			void NormalMapMediaFilter1(const cv::Mat &InNormalMapMat, cv::Mat &OutNormalMapMat, const int windowRadis)const;
			void NormalMapMediaFilterWithDepth(const cv::Mat &InNormalMapMat, cv::Mat &OutNormalMapMat,
				const cv::Mat &InDepthMapMat, cv::Mat &OutDepthMapMat, int windowRadis)const;

			std::vector<cv::Point3f> sparse_normals_;

			std::string GetFileName(const int image_id, const bool isGeom) const;

			void newPropagation(const cv::Mat &joint, const cv::Mat &lowDepthMat, const cv::Mat &lowNormalMat, const float *refK,
				const float upscale, const double sigma_color, const double sigma_space, int radius, const int maxSrcPoint,
				cv::Mat &highDepthMat, cv::Mat &highNormalMat) const;

			void newPropagationFast(const cv::Mat &joint, const cv::Mat &lowDepthMat, const cv::Mat &lowNormalMat, const float *refK,
				const double sigma_color, const double sigma_space, int radius, const int maxSrcPoint,
				cv::Mat &outDepthMat, cv::Mat &outNormalMat) const;

			// ����ΪCV_32F���ݽ��аߵ��˲�����
			template<typename T>
			void FilterSpeckles(cv::Mat & img, T newVal, int maxSpeckleSize, T maxDiff);

			// @even�������ͼ
			void TestDepthmap();

			//��src��enhance�Ľ���ϲ���һ��
			void MergeDepthNormalMaps(const bool haveMerged = false, const bool selectiveJBPF = false);

			//����Ⱥͷ�����ͼ������ѡ���Ե�����˫��(����)�˲���ֵ
			void selJointBilateralPropagateFilter(const cv::Mat& joint, 
				const DepthMap& depthMap, const NormalMap& normalMap,
				const float *refK,
				const double sigma_color, const double sigma_space,
				int radius, const int maxSrcPoint,
				DepthMap& outDepthMap, NormalMap& outNormalMap) const;

			// ������֪�˲�(˫���˲��ı���)Noise-aware filter
			void NoiseAwareFilter(const cv::Mat& joint, 
				DepthMap& depthMap, const NormalMap& normalMap,
				const float* refK,
				const double& sigma_space, const double& sigma_color, const double& sigma_depth,
				const float& THRESH,
				const float& eps, const float& tau,
				const bool is_propagate,
				int radius, 
				DepthMap& outDepthMap, NormalMap& outNormalMap) const;

			//  joint-trilateral-upsampling: JTU
			void JTU(const cv::Mat& joint,
				DepthMap& depthMap, const NormalMap& normalMap,
				const float* refK,
				const double& sigma_space, const double& sigma_color, double& sigma_depth,
				const float& THRESH,
				const bool is_propagate,
				int radius,
				DepthMap& outDepthMap, NormalMap& outNormalMap) const;

			//�����ͼ�ͷ�����ͼ��������˫���˲�
			void jointBilateralFilter_depth_normal_maps(const cv::Mat &joint, const DepthMap &depthMap, const NormalMap &normalMap,
				const float *refK, const double sigma_color, const double sigma_space, int radius,
				DepthMap &outDepthMap, NormalMap &outNormalMap) const;

			void distanceWeightFilter(const DepthMap &depthMap, const NormalMap &normalMap,
				const float *refK, const double sigma_color, const double sigma_space, int radius,
				DepthMap &outDepthMap, NormalMap &outNormalMap) const;

		private:

			Options options_;
			Model model_;
			bool hasReadMapsPhoto_;  // �Ƿ����ͼ��һ����mapͼ
			bool hasReadMapsGeom_;  // �Ƿ���뼸��һ����mapͼ�����߲���ͬʱΪtrue��
			std::vector<bool> hasBitMaps_;
			std::vector<cv::Mat> bitMaps_;
			std::vector<DepthMap> m_depth_maps;
			std::vector<NormalMap> m_normal_maps;
			std::vector<std::pair<float, float>> depth_ranges_;
			std::vector<cv::Mat> slicLabels_;

		};

	}  // namespace mvs
}  // namespace colmap

#endif  // COLMAP_SRC_MVS_WORKSPACE_H_
