#include "workspace.h"

#include "MyPatchMatch.h"

#include <io.h>
#include <ctime>

#include <opencv2/ximgproc.hpp>
#include "JointBilateralFilter.h"
#include "upsampler.h"//˫�������ϲ���
#include "BilateralGrid.h"//˫������
#include "FastBilateralSolverMe.h"
#include "guidedfilter.h"

//#include "BilateralTextureFilter.h";//ϸ�ںͽṹ��ǿ

//#include <Eigen\core>

#include "Utils.h"

namespace colmap {
	namespace mvs {

		//��� * elem1 Ӧ������ * elem2 ǰ�棬��������ֵ�Ǹ��������κθ��������У���
		//��� * elem1 ��* elem2 �ĸ�����ǰ�涼�У���ô��������0
		//��� * elem1 Ӧ������ * elem2 ���棬��������ֵ�����������κ����������У���
		bool pairIfAscend(pair<float, int> &a, pair<float, int> &b)
		{
			//if (a.second >= b.second)//���aҪ����b���棬����������
			//{
			//	return 1;
			//}
			//else
			//{
			//	return -1;
			//}

			return a.first < b.first;
		}

		//�����ɴ�С
		bool pairIfDescend(pair<float, int> &a, pair<float, int> &b)
		{
			return a.first > b.first;
		}

		template <typename T>
		float Median(std::vector<T>* elems)
		{
			assert(!elems->empty());
			const size_t mid_idx = elems->size() / 2;
			std::nth_element(elems->begin(), elems->begin() + mid_idx, elems->end());
			if (elems->size() % 2 == 0)
			{
				const float mid_element1 = static_cast<float>((*elems)[mid_idx]);
				const float mid_element2 = static_cast<float>(
					*std::max_element(elems->begin(), elems->begin() + mid_idx));
				return (mid_element1 + mid_element2) / 2.0f;
			}
			else
			{
				return static_cast<float>((*elems)[mid_idx]);
			}
		}

		// ���캯��: ��ʼ��
		Workspace::Workspace(const Options& options)
			: options_(options)
		{
			// ��bundler�����ж�ȡϡ�������Ϣ
			// StringToLower(&options_.input_type);

			// ����ԭͼ���·��
			this->model_.SetSrcImgRelDir(std::string(options.src_img_dir));

			// ��ȡ�����ռ�(SFMϡ���ؽ����)
			model_.Read(options_.workspace_path,
				options_.workspace_format,
				options_.newPath);

			if (options_.max_image_size != -1)
			{
				for (auto& image : model_.m_images)
				{
					// ������ͼ��ߴ�
					image.Downsize(options_.max_image_size, options_.max_image_size);
				}
			}
			if (options_.bDown_sampling)  // �Ƿ���н���������
			{
				for (auto& image : model_.m_images)
				{
					// ͼ���������������ű���
					image.Rescale(options_.fDown_scale);
				}
			}

			// ������ͼ�����ȥ���䴦��
			//model_.RunUndistortion(options_.undistorte_path);

			// ����bundler����ά��ͶӰ����ά��
			model_.ProjectToImage();

			// ������ȷ�Χ
			depth_ranges_ = model_.ComputeDepthRanges();

			// ��ʼ״̬����û���������map����
			hasReadMapsPhoto_ = false;
			hasReadMapsGeom_ = false;
			hasBitMaps_.resize(model_.m_images.size(), false);
			bitMaps_.resize(model_.m_images.size());

			// ��ʼ�����ͼ������С
			this->m_depth_maps.resize(model_.m_images.size());
			this->m_normal_maps.resize(model_.m_images.size());
		}

		void Workspace::runSLIC(const std::string &path)
		{
			std::cout << "\t" << "=> Begin SLIC..." << std::endl;

			slicLabels_.resize(model_.m_images.size());

			int k = 1500;
			int m = 10;
			float ss = 15;  // �����صĲ���
			for (int i = 0; i < model_.m_images.size(); i++)
			{
				SLIC *slic = new SLIC(ss, m, model_.m_images[i].GetPath(), path);
				slic->run(i).copyTo(slicLabels_[i]);//�ѳ����طָ������labelͼ������slicLabels_
				delete slic;
			}

			std::cout << "\t" << "Done SLIC" << std::endl;
		}

		void Workspace::showImgPointToSlicImage(const std::string &path)
		{
			//for (const auto &point : model_.points)
			//{
			//	//const cv::Mat cvPoint=(cv::Mat_<float>(4,1)<< point.x, point.y, point.z, 1.0f);
			//	const Eigen::Vector4f pt(point.x, point.y, point.z, 1.0f);
			//	for (size_t i = 0; i < point.track.size(); i++)
			//	{
			//		const int img_id = point.track[i];
			//		const auto &image = model_.images.at(img_id);
			//		const Eigen::Vector3f xyz = Eigen::Map<const Eigen::Matrix<float, 3, 4, Eigen::RowMajor>>
			//			(image.GetP())*pt;
			//		const cv::Point3f cvImgPoint(xyz(0), xyz(1), xyz(2));
			//		model_.imagePoints.at(img_id).push_back(cvImgPoint);
			//		//��������Ϣ�����Լ��ͶӰ���Ƿ��bundler������һ��
			//		float x = xyz(0) / xyz(2) - image.GetWidth() / 2;
			//		float y = -xyz(1) / xyz(2) + image.GetHeight() / 2;
			//
			//	}
			//}

			/////����ά��ͶӰ��ͼ������
			for (int img_id = 0; img_id < model_.m_images.size(); img_id++)
			{
				const auto &image = model_.m_images.at(img_id);
				cv::Mat img = cv::imread(image.GetPath());

				//��Сͼ�񱥺Ͷȣ��ǵ���άͶӰ����ͼ�����濴�ĸ����
				cv::Mat whiteImg(img.size(), img.type(), cv::Scalar::all(255));
				cv::addWeighted(img, 0.6, whiteImg, 0.4, 0.0, img);

				//��ʼ��ͼ������Բ
				for (const auto &imgPt : model_.m_img_pts.at(img_id))
				{
					const cv::Point2d pp(imgPt.x / imgPt.z, imgPt.y / imgPt.z);
					cv::circle(img, pp, 1, cv::Scalar(0, 0, 255), -1, 8);
				}

				char filename[20];
				sprintf_s(filename, "SparsePoject%d.jpg", img_id);
				const string filePath = path + filename;
				imwrite(filePath, img);
			}

			//��ͶӰ�㻭��������ͼ������
			for (int img_id = 0; img_id < model_.m_images.size(); img_id++)
			{
				const auto &image = model_.m_images.at(img_id);
				const auto &label = slicLabels_.at(img_id);
				cv::Mat img = cv::imread(image.GetPath());

				//����ͼ�񱥺Ͷȣ�ʹ�ö�ά����ͼ���Ͽ�������
				cv::Mat whiteImg(img.rows, img.cols, CV_8UC3, cv::Scalar::all(255));
				cv::addWeighted(img, 0.8, whiteImg, 0.2, 0.0, img);

				////��������
				int dx8[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
				int dy8[8] = { 0, -1, -1, -1, 0, 1, 1, 1 };

				cv::Mat istaken(img.rows, img.cols, CV_8UC1, cv::Scalar::all(false));
				for (int i = 0; i < img.rows; i++)
				{
					for (int j = 0; j < img.cols; j++)
					{
						int np = 0;
						for (int k = 0; k < 8; k++)
						{
							int x = j + dx8[k];
							int y = i + dy8[k];

							if (x > -1 && x < img.cols && y > -1 && y < img.rows)
							{
								if (istaken.at<bool>(y, x) == false)
								{
									if (label.at<int>(i, j) != label.at<int>(y, x))
									{
										np++;
									}
								}
							}
						}
						if (np > 1)  //����ɼ�ϸ�����طָ���
						{
							img.at<Vec3b>(i, j) = Vec3b(255, 255, 255);//����
							//img.at<Vec3b>(i, j) = Vec3b(0, 0, 0);//����
							istaken.at<bool>(i, j) = true;
						}
					}
				}

				////��ʼ��ͼ������Բ
				for (const auto &imgPt : model_.m_img_pts.at(img_id))
				{
					const cv::Point2d pp(imgPt.x / imgPt.z, imgPt.y / imgPt.z);
					cv::circle(img, pp, 1, cv::Scalar(0, 0, 255), -1, 8);
				}

				char filename[20];
				sprintf_s(filename, "slicPoject%d.jpg", img_id);
				const string filePath = path + filename;
				imwrite(filePath, img);
			}
		}

		////----------------------------------------------------------------------////
		//����Ⱥͷ�����ͼ�����ϲ���
		void Workspace::UpSampleMapAndModel()
		{
		}

		const Model& Workspace::GetModel() const { return model_; }

		const cv::Mat& Workspace::GetBitmap(const int img_id)
		{
			if (!hasBitMaps_.at(img_id))
			{
				std::string img_path = model_.m_images.at(img_id).GetPath();

				// @even
				StringReplace(img_path, std::string("MyMvs"),
					std::string(this->model_.m_src_img_rel_dir));

				//printf("img_path: %s", img_path.c_str());
				cv::Mat bitmap = imread(img_path);
				if (bitmap.empty())
				{
					printf("[Err]: empty bitmap!\n");
					return cv::Mat();
				}

				if (!options_.image_as_rgb)  // �������Ҫrgbͼ����ôת��Ϊ�Ҷ�ͼ��
				{
					cv::cvtColor(bitmap, bitmap, CV_BGR2GRAY);
				}

				if (options_.bDetailEnhance)  // �Ƿ�ϸ����ǿ
				{
					//DentailEnhance(bitmap, bitmap);
					//detailEnhance(bitmap, bitmap);//opencv
					const string &tempFileName = "/" + model_.m_images.at(img_id).GetfileName() + ".jpg";
					imwrite(options_.workspace_path + options_.newPath + tempFileName, bitmap);
				}
				else if (options_.bStructureEnhance)//�Ƿ�ṹ��ǿ
				{
					//MultiscaleStructureEnhance(bitmap, bitmap);
					const string &tempFileName = "/" + model_.m_images.at(img_id).GetfileName() + ".jpg";
					imwrite(options_.workspace_path + options_.newPath + tempFileName, bitmap);
				}

				bitMaps_.at(img_id) = bitmap;
				hasBitMaps_.at(img_id) = true;
			}
			return bitMaps_.at(img_id);
		}

		// ����photometirc����geometric��Ⱥͷ���mapͼ
		const void Workspace::ReadDepthAndNormalMaps(const bool isGeometric)
		{
			// ���Ҫ��Geom�����Ѿ����룬����Ҫ��photo�����Ѿ����룬�򷵻�
			if (isGeometric && hasReadMapsGeom_)
			{
				std::cout << "**Have Read geometric depth/normalMaps Before**" << std::endl;
				return;
			}
			else if (!isGeometric && hasReadMapsPhoto_)
			{
				std::cout << "**Have Read photometric depth/normalMaps Before**" << std::endl;
				return;
			}

			// ��ȡ������Ⱥͷ���map
			for (int img_id = 0; img_id < model_.m_images.size(); img_id++)
			{
				//DepthMap depth_map(model_.images.at(image_id).GetWidth(), model_.images.at(image_id).GetHeight(),
				//	depth_ranges_.at(image_id).first, depth_ranges_.at(image_id).second);

				// ��ʼ��depth map
				DepthMap depth_map(depth_ranges_.at(img_id).first,
					depth_ranges_.at(img_id).second);

				string& depth_map_path = this->GetDepthMapPath(img_id, isGeometric);

				depth_map.ReadBinary(depth_map_path);

				// ��Ϊͼ��ߴ�������+-1, ��˾ͼ򵥵İ�ͼ��ߴ��޸�һ��
				const size_t mapWidth = depth_map.GetWidth();
				const size_t mapHeigh = depth_map.GetHeight();
				const size_t imgWidth = model_.m_images.at(img_id).GetWidth();
				const size_t imgHeigh = model_.m_images[img_id].GetHeight();

				assert(mapWidth == imgWidth && mapHeigh == imgHeigh);

				//if (mapWidth!=imgWidth || mapHeigh!=imgHeigh)
				//{
				//	model_.images.at(image_id).SetWidth(mapWidth);
				//	model_.images.at(image_id).SetHeight(mapHeigh);
				//	model_.images.at(image_id).ResizeBitMap(); 
				//	model_.images.at(image_id).WriteBitMap();
				//}
				m_depth_maps.at(img_id) = depth_map;

				NormalMap normal_map;

				string& normal_map_path = GetNormalMapPath(img_id, isGeometric);

				normal_map.ReadBinary(normal_map_path);
				m_normal_maps.at(img_id) = normal_map;

				//depth_map.WriteBinary(GetDepthMapPath(image_id, isGeometric));
				//normal_map.WriteBinary(GetNormalMapPath(image_id, isGeometric));
			}
			if (isGeometric)
			{
				hasReadMapsGeom_ = true;
				hasReadMapsPhoto_ = false;
				std::cout << "**Read geometric depth/normalMap to workspace Done**" << std::endl;
			}
			else
			{
				hasReadMapsPhoto_ = true;
				hasReadMapsGeom_ = false;
				std::cout << "**Read photometric depth/normalMap to workspace Done**" << std::endl;
			}
		}

		const DepthMap& Workspace::GetDepthMap(const int image_id) const
		{
			assert(hasReadMapsPhoto_ || hasReadMapsGeom_);
			return this->m_depth_maps.at(image_id);
		}

		const NormalMap& Workspace::GetNormalMap(const int image_id) const
		{
			assert(hasReadMapsPhoto_ || hasReadMapsGeom_);
			return m_normal_maps.at(image_id);
		}

		const std::vector<DepthMap>& Workspace::GetAllDepthMaps() const
		{
			assert(hasReadMapsPhoto_ || hasReadMapsGeom_);
			return m_depth_maps;
		}

		const std::vector<NormalMap>& Workspace::GetAllNormalMaps() const
		{
			assert(hasReadMapsPhoto_ || hasReadMapsGeom_);
			return m_normal_maps;
		}


		void Workspace::WriteDepthMap(const int image_id, const DepthMap &depthmap)
		{
			m_depth_maps.at(image_id) = depthmap;
		}

		void Workspace::WriteNormalMap(const int image_id, const NormalMap &normalmap)
		{
			m_normal_maps.at(image_id) = normalmap;
		}

		const ConsistencyGraph& Workspace::GetConsistencyGraph(const int image_id) const
		{
			ConsistencyGraph consistecyGraph;
			consistecyGraph.Read(GetConsistencyGaphPath(image_id));
			return consistecyGraph;
		}

		std::string Workspace::GetBitmapPath(const int img_id) const
		{
			return model_.m_images.at(img_id).GetPath();
		}

		std::string Workspace::GetDepthMapPath(const int img_id, const bool isGeom) const
		{
			return model_.m_images.at(img_id).GetDepthMapPath() + GetFileName(img_id, isGeom);
		}

		std::string Workspace::GetNormalMapPath(const int img_id, const bool isGeom) const
		{
			return model_.m_images.at(img_id).GetNormalMapPath() + GetFileName(img_id, isGeom);
		}

		std::string Workspace::GetConsistencyGaphPath(const int image_id) const
		{
			return model_.m_images.at(image_id).GetConsistencyPath() + GetFileName(image_id, false);
		}

		std::string Workspace::GetFileName(const int image_id, const bool isGeom) const 
		{
			const auto& image_name = model_.GetImageName(image_id);

			const std::string file_type = ".bin";
			std::string fileName;
			if (!isGeom)  // ������Ǽ���һ���Եģ�
			{
				fileName = image_name + "." + options_.input_type + file_type;
			}
			else  // ����Ǽ���һ���Ե�
			{
				fileName = image_name + "." + options_.input_type_geom + file_type;
			}
			return fileName;
		}

		float Workspace::GetDepthRange(const int image_id, bool isMax) const
		{
			return isMax ? depth_ranges_.at(image_id).second : depth_ranges_.at(image_id).first;
		}

		bool Workspace::HasBitmap(const int image_id) const
		{
			return hasBitMaps_.at(image_id);
		}

		bool Workspace::HasDepthMap(const int image_id, const bool isGeom) const
		{

			//return (hasReadMapsGeom_ || hasReadMapsPhoto_);
			return _access(GetDepthMapPath(image_id, isGeom).c_str(), 0);
		}

		bool Workspace::HasNormalMap(const int image_id, const bool isGeom) const
		{

			//return (hasReadMapsGeom_ || hasReadMapsPhoto_);
			return _access(GetNormalMapPath(image_id, isGeom).c_str(), 0);
		}

		//����˫���ϲ���
		void Workspace::jointBilateralUpsampling(const cv::Mat &joint, const cv::Mat &lowin, const float upscale,
			const double sigma_color, const double sigma_space, int radius, cv::Mat &highout) const
		{
			highout.create(joint.size(), lowin.type());
			const int highRow = joint.rows;
			const int highCol = joint.cols;
			const int lowRow = lowin.rows;
			const int lowCol = lowin.cols;

			if (radius <= 0)
				radius = round(sigma_space * 1.5);
			const int d = 2 * radius + 1;

			// ԭ����ͼ���ͨ����
			const int cnj = joint.channels();

			float *color_weight = new float[cnj * 256];
			float *space_weight = new float[d*d];
			int *space_ofs_row = new int[d*d];  // ����Ĳ�ֵ
			int *space_ofs_col = new int[d*d];

			double gauss_color_coeff = -0.5 / (sigma_color * sigma_color);
			double gauss_space_coeff = -0.5 / (sigma_space * sigma_space);

			// initialize color-related bilateral filter coefficients  
			// ɫ��ĸ�˹Ȩ��  
			for (int i = 0; i < 256 * cnj; i++)
				color_weight[i] = (float)std::exp(i * i * gauss_color_coeff);

			int maxk = 0;   // 0 - (2*radius + 1)^2  

			// initialize space-related bilateral filter coefficients  
			// �ռ��ĸ�˹Ȩ��
			for (int i = -radius; i <= radius; i++)
			{
				for (int j = -radius; j <= radius; j++)
				{
					double r = std::sqrt((double)i * i + (double)j * j);
					if (r > radius)
						continue;

					// �ռ�Ȩ����������Сͼ���ϵ�
					space_weight[maxk] = (float)std::exp(r * r * gauss_space_coeff / (upscale*upscale));
					space_ofs_row[maxk] = i;
					space_ofs_col[maxk++] = j;
				}
			}

			for (int r = 0; r < highRow; r++)
			{
				for (int l = 0; l < highCol; l++)
				{
					int px = l, py = r;  // ������������
					//float fpx = (float)px / upscale;
					//float fpy = (float)py / upscale;
					const cv::Vec3b color0 = joint.ptr<cv::Vec3b>(py)[px];
					float sum_w = 0;
					float sum_value[3] = { 0 };
					for (int k = 0; k < maxk; k++)
					{
						const int qy = py + space_ofs_row[k];
						const int qx = px + space_ofs_col[k];

						if (qx < 0 || qx >= highCol || qy < 0 || qy >= highRow)
							continue;

						float fqx = (float)qx / upscale;//�ͷֱ���ͼ���Ӧ����
						float fqy = (float)qy / upscale;
						int iqx = roundf(fqx);//��������
						int iqy = roundf(fqy);
						if (iqx >= lowCol || iqy >= lowRow)
							continue;

						// ��ɫ����Ȩ�أ��������ڸ߷ֱ���ͼ���ϵ�
						cv::Vec3b color1 = joint.ptr<cv::Vec3b>(qy)[qx];

						// ����joint��ǰ���غ��������ص� ����Ȩ�� �� ɫ��Ȩ�أ������ۺϵ�Ȩ��  
						float w = space_weight[k] * color_weight[abs(color0[0] - color1[0]) + abs(color0[1] - color1[1]) + abs(color0[2] - color1[2])];

						if (lowin.type() == CV_8UC3)
						{
							sum_value[0] += lowin.ptr<cv::Vec3b>(iqy)[iqx][0] * w;
							sum_value[1] += lowin.ptr<cv::Vec3b>(iqy)[iqx][1] * w;
							sum_value[2] += lowin.ptr<cv::Vec3b>(iqy)[iqx][2] * w;
						}
						else if (lowin.type() == CV_8UC1)
						{
							sum_value[0] += lowin.ptr<uchar>(iqy)[iqx] * w;
						}
						else if (lowin.type() == CV_32FC3)
						{
							sum_value[0] += lowin.ptr<cv::Vec3f>(iqy)[iqx][0] * w;
							sum_value[1] += lowin.ptr<cv::Vec3f>(iqy)[iqx][1] * w;
							sum_value[2] += lowin.ptr<cv::Vec3f>(iqy)[iqx][2] * w;
						}
						else if (lowin.type() == CV_32FC1)
						{
							sum_value[0] += lowin.ptr<float>(iqy)[iqx] * w;
						}
						sum_w += w;
					}
					//for (int i = -radius; i <= radius; i++)
					//{
					//	for (int j = -radius; j <= radius; j++)
					//	{
					//		int qx = px + j, qy = py + i;//����������
					//		if (qx < 0 || qx >= highCol || qy < 0 || qy >= highRow)
					//			continue;
					//
					//		float fqx = (float)qx / upscale;//�ͷֱ���ͼ���Ӧ����
					//		float fqy = (float)qy / upscale;
					//		int iqx = roundf(fqx);//��������
					//		int iqy = roundf(fqy);
					//		if (iqx >= lowCol || iqy >= lowRow)
					//			continue;
					//
					//		//�ռ����Ȩ�أ��������ڵͷֱ���ͼ���ϵ�
					//		float spaceDis = (i*i + j*j) / (upscale*upscale);
					//		float space_w = (float)std::exp(spaceDis * gauss_space_coeff);
					//		//��ɫ����Ȩ�أ��������ڸ߷ֱ���ͼ���ϵ�
					//		cv::Vec3b color1 = joint.ptr<cv::Vec3b>(qy)[qx];
					//		float color_w = color_weight[abs(color0[0] - color1[0]) + abs(color0[1] - color1[1]) + abs(color0[2] - color1[2])];
					//
					//		float w = space_w*color_w;
					//		if (lowin.type()==CV_8UC3)
					//		{
					//			sum_value[0] += lowin.ptr<cv::Vec3b>(iqy)[iqx][0] * w;
					//			sum_value[1] += lowin.ptr<cv::Vec3b>(iqy)[iqx][1] * w;
					//			sum_value[2] += lowin.ptr<cv::Vec3b>(iqy)[iqx][2] * w;
					//		}
					//		else if (lowin.type()==CV_8UC1)
					//		{
					//			sum_value[0] += lowin.ptr<uchar>(iqy)[iqx] * w;
					//		}
					//		else if (lowin.type() == CV_32FC3)
					//		{
					//			sum_value[0] += lowin.ptr<cv::Vec3f>(iqy)[iqx][0] * w;
					//			sum_value[1] += lowin.ptr<cv::Vec3f>(iqy)[iqx][1] * w;
					//			sum_value[2] += lowin.ptr<cv::Vec3f>(iqy)[iqx][2] * w;
					//		}
					//		else if (lowin.type() == CV_32FC1)
					//		{
					//			sum_value[0] += lowin.ptr<float>(iqy)[iqx] * w;
					//		}
					//		sum_w += w;
					//	}
					//}
					sum_w = 1.f / sum_w;
					if (lowin.type() == CV_8UC3)
					{
						highout.ptr<cv::Vec3b>(py)[px] = cv::Vec3b(sum_value[0] * sum_w, sum_value[1] * sum_w, sum_value[2] * sum_w);
					}
					else if (lowin.type() == CV_8UC1)
					{
						highout.ptr<uchar>(py)[px] = sum_value[0] * sum_w;
					}
					else if (lowin.type() == CV_32FC3)
					{
						highout.ptr<cv::Vec3f>(py)[px] = cv::Vec3f(sum_value[0] * sum_w, sum_value[1] * sum_w, sum_value[2] * sum_w);
					}
					else if (lowin.type() == CV_32FC1)
					{
						highout.ptr<float>(py)[px] = sum_value[0] * sum_w;
					}

				}
			}
		}

		// ����˫�ߴ����ϲ���
		void Workspace::jointBilateralPropagationUpsampling(const cv::Mat &joint, const cv::Mat &lowDepthMat, const cv::Mat &lowNormalMat, const float *refK,
			const float upscale, const double sigma_color, const double sigma_space, const int radius, cv::Mat &highDepthMat) const
		{

		}

		// ����˫�ߴ����˲�
		void Workspace::jointBilateralDepthMapFilter1(const cv::Mat &srcDepthMap, const cv::Mat &srcNormalMap, const cv::Mat &srcImage, const float *refK,
			const int radius, const double sigma_color, const double sigma_space, DepthMap &desDepMap, NormalMap &desNorMap, const bool DoNormal)const
		{

		}

		float Workspace::PropagateDepth(const float *refK,
			const float depth_1, const float normal_1[3],
			const float row_1, const float col_1, const float row_2, const float col_2) const
		{
			// Extract 1/fx, -cx/fx, 1/fy, -cy/fy.
			const float ref_inv_K[4] = { 1.0f / refK[0], -refK[2] / refK[0], 1.0f / refK[4], -refK[5] / refK[4] };

			// Point along first viewing ray.
			const float p1[3] = {
				depth_1 * (ref_inv_K[0] * col_1 + ref_inv_K[1]),
				depth_1 * (ref_inv_K[2] * row_1 + ref_inv_K[3]),
				depth_1
			};

			// Point on second viewing ray.
			const float p2[3] = {
				ref_inv_K[0] * col_2 + ref_inv_K[1],
				ref_inv_K[2] * row_2 + ref_inv_K[3],
				1.0f
			};

			const float denom = (p1[0] * normal_1[0] + p1[1] * normal_1[1] + p1[2] * normal_1[2]) /
				(p2[0] * normal_1[0] + p2[1] * normal_1[1] + p2[2] * normal_1[2]);

			const float lowDepth = depth_1 * 0.95;
			const float highDepth = depth_1 * 1.05;

			//cout << row1 << "," << col1 << " --->" << row2 << "," << col2 << endl;
			//cout << depth1<<"--->"<< denom <<"  (" <<lowDepth << "," << highDepth <<")"<< endl;

			return denom < lowDepth ? lowDepth : (denom > highDepth ? highDepth : denom);
		}

		void Workspace::SuitNormal(const int row, const int col,
			const float* refK, float normal[3]) const
		{

			// Extract 1/fx, -cx/fx, 1/fy, -cy/fy.
			const float ref_inv_K[4] = {
				1.0f / refK[0],            // 1/fx
				-refK[2] / refK[0],        // -cx/fx
				1.0f / refK[4],            // 1/fy
				-refK[5] / refK[4]         // -cy/fy
			};

			// Make sure the perturbed normal is still looking in the same direction as
			// the viewing direction.
			const float view_ray[3] = {
				ref_inv_K[0] * col + ref_inv_K[1],
				ref_inv_K[2] * row + ref_inv_K[3],
				1.0f
			};
			if ((normal[0] * view_ray[0] + normal[1] * view_ray[1] + normal[2] * view_ray[2]) >= 0.0f)
			{

				normal[0] *= -1.0f;
				normal[1] *= -1.0f;
				normal[2] *= -1.0f;
			}

			// Make sure normal has unit norm.
			float norm = sqrt(normal[0] * normal[0]
				+ normal[1] * normal[1] + normal[2] * normal[2]);
			if (norm < 1e-8)
			{
				//cout << "[Warning]: very small normal L2 norm!" << endl;
				norm += float(1e-8);
			}

			// ������ǵ�λ��������ô��һ��: ����һ����Сֵ�������
			const float inv_norm = 1.0f / norm;
			if (inv_norm != 1.0f)
			{
				normal[0] *= inv_norm;
				normal[1] *= inv_norm;
				normal[2] *= inv_norm;
			}
		}

		// �Է�����ͼ��������ֵ�˲�
		void Workspace::NormalMapMediaFilter(const cv::Mat& InNormalMapMat,
			cv::Mat& OutNormalMapMat, const int windowRadis) const
		{

		}

		// �Է�����ͼ��������ֵ�˲����޳�Ϊ0������
		void Workspace::NormalMapMediaFilter1(const cv::Mat &InNormalMapMat, cv::Mat &OutNormalMapMat, const int windowRadis) const
		{

		}

		// �Է����������ͼ��������ֵ�˲��������޳�Ϊ0������
		void Workspace::NormalMapMediaFilterWithDepth(const cv::Mat &InNormalMapMat, cv::Mat &OutNormalMapMat,
			const cv::Mat &InDepthMapMat, cv::Mat &OutDepthMapMat, int windowRadis) const
		{

		}

		void Workspace::newPropagation(const cv::Mat &joint, const cv::Mat &lowDepthMat, const cv::Mat &lowNormalMat, const float *refK,
			const float upscale, const double sigma_color, const double sigma_space, int radius, const int maxSrcPoint,
			cv::Mat &highDepthMat, cv::Mat &highNormalMat) const
		{

		}

		void Workspace::newPropagationFast(const cv::Mat &joint, const cv::Mat &lowDepthMat, const cv::Mat &lowNormalMat,
			const float *refK, const double sigma_color, const double sigma_space, int radius, const int maxSrcPoint,
			cv::Mat &outDepthMat, cv::Mat &outNormalMat) const
		{

		}

		// CV_32F����FilterSpeckles
		typedef cv::Point_<short> Point2s;
		//typedef cv::Point_<float> Point2s;

		template <typename T>
		void Workspace::FilterSpeckles(cv::Mat& img, T newVal, int maxSpeckleSize, T maxDiff)
		{
			using namespace cv;

			cv::Mat _buf;

			int width = img.cols, height = img.rows, npixels = width * height;

			// each pixel contains: pixel coordinate(Point2S), label(int), �Ƿ���blob(uchar)
			size_t bufSize = npixels * (int)(sizeof(Point2s) + sizeof(int) + sizeof(uchar));
			if (!_buf.isContinuous() || _buf.empty() || _buf.cols*_buf.rows*_buf.elemSize() < bufSize)
				_buf.reserveBuffer(bufSize);

			uchar* buf = _buf.ptr();
			int i, j, dstep = (int)(img.step / sizeof(T));
			int* labels = (int*)buf;
			buf += npixels * sizeof(labels[0]);
			Point2s* wbuf = (Point2s*)buf;
			buf += npixels * sizeof(wbuf[0]);
			uchar* rtype = (uchar*)buf;
			int curlabel = 0;

			// clear out label assignments
			memset(labels, 0, npixels * sizeof(labels[0]));

			for (i = 0; i < height; i++)
			{
				T* ds = img.ptr<T>(i);
				int* ls = labels + width * i;

				for (j = 0; j < width; j++)
				{
					if (ds[j] != newVal)   // not a bad disparity
					{
						if (ls[j])     // has a label, check for bad label
						{
							if (rtype[ls[j]]) // small region, zero out disparity
								ds[j] = (T)newVal;
						}
						// no label, assign and propagate
						else
						{
							Point2s* ws = wbuf; // initialize wavefront
							Point2s p((short)j, (short)i);  // current pixel
							curlabel++; // next label
							int count = 0;  // current region size
							ls[j] = curlabel;

							// wavefront propagation
							while (ws >= wbuf) // wavefront not empty
							{
								count++;
								// put neighbors onto wavefront
								T* dpp = &img.at<T>(p.y, p.x);
								T dp = *dpp;
								int* lpp = labels + width * p.y + p.x;

								// down neighbor
								if (p.y < height - 1 && !lpp[+width] && dpp[+dstep] != newVal && std::abs(dp - dpp[+dstep]) <= maxDiff)
								{
									lpp[+width] = curlabel;
									*ws++ = Point2s(p.x, p.y + 1);
								}

								// top neighbor
								if (p.y > 0 && !lpp[-width] && dpp[-dstep] != newVal && std::abs(dp - dpp[-dstep]) <= maxDiff)
								{
									lpp[-width] = curlabel;
									*ws++ = Point2s(p.x, p.y - 1);
								}

								// right neighbor
								if (p.x < width - 1 && !lpp[+1] && dpp[+1] != newVal && std::abs(dp - dpp[+1]) <= maxDiff)
								{
									lpp[+1] = curlabel;
									*ws++ = Point2s(p.x + 1, p.y);
								}

								// left neighbor
								if (p.x > 0 && !lpp[-1] && dpp[-1] != newVal && std::abs(dp - dpp[-1]) <= maxDiff)
								{
									lpp[-1] = curlabel;
									*ws++ = Point2s(p.x - 1, p.y);
								}

								// pop most recent and propagate
								// NB: could try least recent, maybe better convergence
								p = *--ws;
							}

							// assign label type
							if (count <= maxSpeckleSize)   // speckle region
							{
								rtype[ls[j]] = 1;   // small region label
								ds[j] = (T)newVal;
							}
							else
								rtype[ls[j]] = 0;   // large region label
						}
					}
				}
			}
		}

		void Workspace::TestDepthmap()
		{
			const string depth_dir = this->options_.workspace_path \
				+ "/dense/stereo/depth_maps/dslr_images_undistorted/";
			
			for (int img_id = 0; img_id < int(this->model_.m_images.size()); ++img_id)
			{
				string file_name = GetFileName(img_id, true);

				DepthMap depth_map(this->depth_ranges_.at(img_id).first,
					this->depth_ranges_.at(img_id).second);
				depth_map.ReadBinary(depth_dir + file_name);
				printf("%s read\n", file_name.c_str());

				// ----------- Test output depth map for visualization
				string depth_map_path = depth_dir + file_name + ".jpg";

				//imwrite(depth_map_path, depth_map.ToBitmapGray(2, 98));
				//printf("%s written\n", string(file_name + string(".jpg")).c_str());

				// ----------- Test se=peckle filtering using CV_16S data
				//cv::Mat depth_mat = depth_map.Depth2Mat();

				//int maxSpeckleSize = int(depth_mat.cols * depth_mat.rows / 5000.0f);
				//int maxDiff = int(0.1f * (depth_map.GetDepthMax() - depth_map.GetDepthMin()));

				//depth_mat.convertTo(depth_mat, CV_16S);

				//// speckle filtering
				//cv::filterSpeckles(depth_mat, 0, maxSpeckleSize, maxDiff);

				//depth_mat.convertTo(depth_mat, CV_32F);
				//depth_map.fillDepthWithMat(depth_mat);

				//imwrite(depth_map_path, depth_map.ToBitmapGray(2, 98));
				//printf("%s written\n", depth_map_path.c_str());

				// ----------- Test se=peckle filtering using CV_32F data
				int maxSpeckleSize = int(depth_map.GetWidth() * depth_map.GetHeight() / 100.0f);
				float maxDiff = 0.1f * (depth_map.GetDepthMax() - depth_map.GetDepthMin());

				cv::Mat depth_mat = depth_map.Depth2Mat();

				// speckle filtering
				this->FilterSpeckles<float>(depth_mat, 0, maxSpeckleSize, maxDiff);

				depth_map.fillDepthWithMat(depth_mat);

				file_name += "_filterSpecke_32F_win11.jpg";
				depth_map_path = depth_dir + file_name;
				imwrite(depth_map_path, depth_map.ToBitmapGray(2, 98));
				printf("%s written\n\n", file_name.c_str());
			}
		}

		// �ϲ�����depth, normal maps(src and enhance)�����ҽ��к�������ѡ��������˫�ߴ�����ֵ
		void Workspace::MergeDepthNormalMaps(const bool is_merged, const bool is_sel_JBPF)
		{
			////ָ���������ͼ�ͷ�����ͼ��·��
			// ԭʼͼ��
			const string DepthPath_src = options_.workspace_path + "/SrcMVS/depth_maps/dslr_images_undistorted/";
			const string NormalPath_src = options_.workspace_path + "/SrcMVS/normal_maps/dslr_images_undistorted/";

			// ϸ����ǿͼ��
			//const string DepthPath_detailEnhance = options_.workspace_path + "/detailEnhance/depth_maps/dslr_images_undistorted/";
			//const string NormalPath_detailEnhance = options_.workspace_path + "/detailEnhance/normal_maps/dslr_images_undistorted/";

			// �ṹ��ǿͼ��
			const string DepthPath_structEnhance = options_.workspace_path + "/EnhanceMVS/depth_maps/dslr_images_undistorted/";
			const string NormalPath_structEnhance = options_.workspace_path + "/EnhanceMVS/normal_maps/dslr_images_undistorted/";

			// �ϲ�������Ⱥͷ�����ͼ�Ľ��·��
			const string resultDepthPath = options_.workspace_path + "/result/depth_maps/";
			const string resultNormalPath = options_.workspace_path + "/result/normal_maps/";

			// �Ժϲ������������ѡ��������˫�ߴ�����ֵ���·��
			const string resultProDepthPath = options_.workspace_path + "/resultPro/depth_maps/";
			const string resultProNormalPath = options_.workspace_path + "/resultPro/normal_maps/";

			// ԭʼ��ɫͼ��·��
			const string srcColorImgPath = options_.workspace_path + "/SrcMVS/images/dslr_images_undistorted/";

			clock_t T_start, T_end;

			// ����ÿһ��ͼ
			for (int img_id = 0; img_id < model_.m_images.size(); img_id++)
			{
				const string DepthAndNormalName = GetFileName(img_id, true);

				// �����û�кϲ�������ô�ϲ�
				if (!is_merged)
				{
					T_start = clock();

					// �ֱ��ȡ���ͼ�ͷ�����ͼ
					DepthMap depthMap_src(depth_ranges_.at(img_id).first,
						depth_ranges_.at(img_id).second),
						//depthMap_detailEnhance(depth_ranges_.at(image_id).first,
						//depth_ranges_.at(image_id).second),
						depthMap_structEnhance(depth_ranges_.at(img_id).first,
							depth_ranges_.at(img_id).second);

					depthMap_src.ReadBinary(DepthPath_src + DepthAndNormalName);
					//depthMap_detailEnhance.ReadBinary(DepthPath_detailEnhance + DepthAndNormalName);
					depthMap_structEnhance.ReadBinary(DepthPath_structEnhance + DepthAndNormalName);

					// normal maps
					NormalMap normalMap_src, normalMap_detailEnhance, normalMap_structEnhance;

					normalMap_src.ReadBinary(NormalPath_src + DepthAndNormalName);
					//normalMap_detailEnhance.ReadBinary(NormalPath_detailEnhance + DepthAndNormalName);
					normalMap_structEnhance.ReadBinary(NormalPath_structEnhance + DepthAndNormalName);

					// write BitMap to local
					const auto& depthMap_path = DepthPath_src + DepthAndNormalName + ".jpg";
					const auto& normalMap_path = NormalPath_src + DepthAndNormalName + ".jpg";

					imwrite(depthMap_path, depthMap_src.ToBitmapGray(2, 98));
					imwrite(normalMap_path, normalMap_src.ToBitmap());
					//imwrite(DepthPath_detailEnhance + DepthAndNormalName + ".jpg", depthMap_detailEnhance.ToBitmapGray(2, 98));
					//imwrite(NormalPath_detailEnhance + DepthAndNormalName + ".jpg", normalMap_detailEnhance.ToBitmap());

					const auto& depthMap_path_struct_enhance = DepthPath_structEnhance + DepthAndNormalName + ".jpg";
					const auto& normalMap_path_struct_enhance = NormalPath_structEnhance + DepthAndNormalName + ".jpg";

					imwrite(depthMap_path_struct_enhance, depthMap_structEnhance.ToBitmapGray(2, 98));
					imwrite(normalMap_path_struct_enhance, normalMap_structEnhance.ToBitmap());

					//depthMaps_.at(image_id) = depthMap_structureEnhance;
					//normalMaps_.at(image_id) = normalMap_structureEnhance;
					//hasReadMapsGeom_= true;

					// @even Fusion���ͼ, ����ͼѡ���߽�С����Ϊ�Լ��Ŀ��
					const int src_width = depthMap_src.GetWidth();
					const int enhance_width = depthMap_structEnhance.GetWidth();
					const int src_height = depthMap_src.GetHeight();
					const int enhance_height = depthMap_structEnhance.GetHeight();

					const int Fusion_Width = std::min(src_width, enhance_width);
					const int Fusion_Height = std::min(src_height, enhance_height);

					//const int width = depthMap_src.GetWidth();
					//const int height = depthMap_src.GetHeight();

					// ��ʼ��Fusion�����ͼ, ����ͼΪ0
					DepthMap depthMap_result(Fusion_Width, Fusion_Height,
						depth_ranges_.at(img_id).first,
						depth_ranges_.at(img_id).second);
					NormalMap normalMap_result(Fusion_Width, Fusion_Height);

					const float NON_VALUE = 0.0f;

					for (int row = 0; row < Fusion_Height; row++)
					{
						for (int col = 0; col < Fusion_Width; col++)
						{
							const float depth_src = depthMap_src.GetDepth(row, col);

							//const float depth_detailEnhance = 
							// depthMap_detailEnhance.Get(row, col);
							const float depth_structEnhance = depthMap_structEnhance.GetDepth(row, col);

							// ��ʼ������ֵΪ0
							float normal_src[3],
								//normal_detailEnhance[3],
								normal_structEnhance[3],
								normal_result[3] = { 0.0f };

							normalMap_src.GetSlice(row, col, normal_src);
							//normalMap_detailEnhance.GetSlice(row, col, normal_detailEnhance);
							normalMap_structEnhance.GetSlice(row, col, normal_structEnhance);

							// �ռ����õ���Ⱥͷ�����Ϣ
							vector<float> depths;
							vector<float*> normals;

							// �ռ����õ�src���,����
							if (depth_src != NON_VALUE)
							{
								depths.push_back(depth_src);
								normals.push_back(normal_src);
							}

							// �ռ����õ�enhance���, ����
							int flags_se = 1;
							if (flags_se == 1 && depth_structEnhance != NON_VALUE)
							{
								depths.push_back(depth_structEnhance);
								normals.push_back(normal_structEnhance);
							}

							//int flags_de = -1;
							//if (flags_de == 1 && depth_detailEnhance != NON_VALUE)
							//{
							//	depths.push_back(depth_detailEnhance);
							//	normals.push_back(normal_detailEnhance);
							//}

							const float num_valid = depths.size();

							if (num_valid > NON_VALUE)
							{
								//// average
								if (0)
								{
									depthMap_result.Set(row,
										col,
										accumulate(depths.begin(), depths.end(), 0.0) / num_valid);

									for (int i = 0; i < num_valid; i++)
									{
										normal_result[0] += normals[i][0];
										normal_result[1] += normals[i][1];
										normal_result[2] += normals[i][2];
									}

									NormVec3(normal_result);
									normalMap_result.SetSlice(row, col, normal_result);
								}
								//// the first
								if (0)
								{
									depthMap_result.Set(row, col, depths[0]);
									normalMap_result.SetSlice(row, col, normals[0]);
								}
								//// evalution
								if (1)
								{
									if (num_valid == 1)
									{
										depthMap_result.Set(row, col, depths[0]);
										normalMap_result.SetSlice(row, col, normals[0]);
									}
									if (num_valid == 2)
									{
										// �������С����ֵ��ȡ���ֵ��С��
										if (abs(depths[0] - depths[1]) / depths[0] > 0.01)
										{
											depthMap_result.Set(row, col, depths[0] < depths[1]
												? depths[0] : depths[1]);
											normalMap_result.SetSlice(row, col, depths[0] < depths[1]
												? normals[0] : normals[1]);
										}
										else  // ���ȡ��ֵ, �������������L2 norm
										{
											depthMap_result.Set(row,
												col,
												(depths[0] + depths[1]) / 2.0f);

											normal_result[0] = normals[0][0] + normals[1][0];
											normal_result[1] = normals[0][1] + normals[1][1];
											normal_result[2] = normals[0][2] + normals[1][2];

											NormVec3(normal_result);
											normalMap_result.SetSlice(row, col, normal_result);
										}
									}
									if (num_valid == 3)
									{
										depthMap_result.Set(row, col, depths[0] >= depths[1] ?
											(depths[1] >= depths[2] ? depths[1] : (depths[0] >= depths[2]
												? depths[2] : depths[0])) :
												(depths[0] >= depths[2] ? depths[0] : (depths[1] >= depths[2]
													? depths[2] : depths[1])));
										normalMap_result.SetSlice(row, col, depths[0] >= depths[1] ?
											(depths[1] >= depths[2] ? normals[1] : (depths[0] >= depths[2]
												? normals[2] : normals[0])) :
												(depths[0] >= depths[2] ? normals[0] : (depths[1] >= depths[2]
													? normals[2] : normals[1])));
									}
								}
							}

						}  // col
					}  // row

					// ���ù����ռ��depth, normal mapsΪ�ϲ������˺��ֵ
					m_depth_maps.at(img_id) = depthMap_result;
					m_normal_maps.at(img_id) = normalMap_result;

					// ����"�Ѷ�": ���¼���һ����depth, normal maps�Ķ�ȡ״̬
					hasReadMapsGeom_ = true;

					// ���ϲ����depth, normal mapsд��workspace
					imwrite(resultDepthPath + DepthAndNormalName + ".jpg",
						depthMap_result.ToBitmapGray(2, 98));
					imwrite(resultNormalPath + DepthAndNormalName + ".jpg",
						normalMap_result.ToBitmap());

					depthMap_result.WriteBinary(resultDepthPath + DepthAndNormalName);
					normalMap_result.WriteBinary(resultNormalPath + DepthAndNormalName);

					T_end = clock();
					std::cout << "Merge image:" << img_id << " Time:" << (float)(T_end - T_start) / CLOCKS_PER_SEC << "s" << endl;
				}

				// ��ѡ��������˫�ߴ�����ֵ
				if (is_sel_JBPF)
				{
					T_start = clock();

					// ���֮ǰ�ϲ���Mapͼ�ˣ�ֱ�Ӵ��ļ��ж�ȡ������
					if (is_merged)
					{
						// ��ȡ���ͼ�ͷ�����ͼ
						DepthMap depthMap(depth_ranges_.at(img_id).first,
							depth_ranges_.at(img_id).second);
						depthMap.ReadBinary(resultDepthPath + DepthAndNormalName);

						NormalMap normalMap;
						normalMap.ReadBinary(resultNormalPath + DepthAndNormalName);

						m_depth_maps.at(img_id) = depthMap;
						m_normal_maps.at(img_id) = normalMap;
					}

					// ��������
					DepthMap depthMap_pro = m_depth_maps.at(img_id);
					NormalMap normalMap_pro = m_normal_maps.at(img_id);

					// ��ȡԭ��ɫͼ��resize
					const auto& src_img_path = srcColorImgPath + model_.GetImageName(img_id);
					cv::Mat src_img = imread(src_img_path);
					resize(src_img,
						src_img,
						Size(m_depth_maps.at(img_id).GetWidth(),
							m_depth_maps.at(img_id).GetHeight()));

					//// ѡ��˫�ߴ����˲�
					//this->selJointBilateralPropagateFilter(src_img,
					//	this->m_depth_maps.at(img_id),
					//	this->m_normal_maps.at(img_id),
					//	model_.m_images.at(img_id).GetK(),
					//	25, 10,  // 25, 10
					//	-1, 16,
					//	depthMap_pro, normalMap_pro);

					//// ����SelJointBilateralPropagateFilter
					//int sigma_color = 23, sigma_space = 7;
					//for (int iter_i = 0; iter_i < 3; ++iter_i)
					//{
					//	// ѡ��˫�ߴ����˲�
					//	this->selJointBilateralPropagateFilter(src_img,
					//		this->m_depth_maps.at(img_id),
					//		this->m_normal_maps.at(img_id),
					//		model_.m_images.at(img_id).GetK(),
					//		sigma_color, sigma_space,  // 25, 10
					//		-1, 16,
					//		depthMap_pro, normalMap_pro);

					//	// ��̬����
					//	sigma_color += 1;
					//	sigma_space += 1;

					//	// ���ù����ռ��depth, normal mapsΪ�ϲ������˺��ֵ
					//	this->m_depth_maps.at(img_id) = depthMap_pro;
					//	this->m_normal_maps.at(img_id) = normalMap_pro;
					//}
					
					const int NUM_ITER = 1;  // ��������
					const double sigma_space = 5.0, sigma_color = 5.0, sigma_depth = 5.0;
					const float THRESH = 0.00f, eps = 1.0f, tau = 0.3f;   // ������ 
					const bool is_propagate = false;   // �Ƿ�ʹ�ô������ֵ
					for (int iter_i = 0; iter_i < NUM_ITER; ++iter_i)
					{
						this->NoiseAwareFilter(src_img,
							this->m_depth_maps.at(img_id),
							this->m_normal_maps.at(img_id),
							model_.m_images.at(img_id).GetK(),
							sigma_space, sigma_color, sigma_depth,
							THRESH,
							eps, tau,
							is_propagate,
							25,  // radius, window_size: 2*d + 1
							depthMap_pro, normalMap_pro);

						this->m_depth_maps.at(img_id) = depthMap_pro;
						this->m_normal_maps.at(img_id) = normalMap_pro;

						//// д���м���..
						//if (iter_i % 10 == 0 || iter_i == NUM_ITER - 1)
						//{
						//	char buff[100];
						//	sprintf(buff, "_iter%d.jpg", iter_i);
						//	imwrite(std::move(resultProDepthPath + DepthAndNormalName + string(buff)),
						//		depthMap_pro.ToBitmapGray(2, 98));
						//}
					}

					//const int NUM_ITER = 1;  // ��������
					//const double sigma_space = 1.5, sigma_color = 0.09;
					//double sigma_depth = 0.02;
					//const float THRESH = 0.06f;   // ������ 
					//const bool is_propagate = false;   // �Ƿ�ʹ�ô������ֵ
					//for (int iter_i = 0; iter_i < NUM_ITER; ++iter_i)
					//{
					//	this->JTU(src_img,
					//		this->m_depth_maps.at(img_id),
					//		this->m_normal_maps.at(img_id),
					//		model_.m_images.at(img_id).GetK(),
					//		sigma_space, sigma_color, sigma_depth,
					//		THRESH,
					//		is_propagate,
					//		25,  // radius, window_size: 2*d + 1
					//		depthMap_pro, normalMap_pro);

					//	this->m_depth_maps.at(img_id) = depthMap_pro;
					//	this->m_normal_maps.at(img_id) = normalMap_pro;

					//	//// д���м���..
					//	//if (iter_i % 10 == 0 || iter_i == NUM_ITER - 1)
					//	//{
					//	//	char buff[100];
					//	//	sprintf(buff, "_iter%d.jpg", iter_i);
					//	//	imwrite(std::move(resultProDepthPath + DepthAndNormalName + string(buff)),
					//	//		depthMap_pro.ToBitmapGray(2, 98));
					//	//}
					//}

					// ����"�Ѷ�": ���¼���һ����depth, normal maps�Ķ�ȡ״̬
					hasReadMapsGeom_ = true;

					// ��depth, normal mapsת��bitmap��д�����
					imwrite(resultProDepthPath + DepthAndNormalName + ".jpg",
						depthMap_pro.ToBitmapGray(2, 98));
					imwrite(resultProNormalPath + DepthAndNormalName + ".jpg",
						normalMap_pro.ToBitmap());

					depthMap_pro.WriteBinary(resultProDepthPath + DepthAndNormalName);
					normalMap_pro.WriteBinary(resultProNormalPath + DepthAndNormalName);

					T_end = clock();

					cout << "SelectiveJBPF image:" << img_id << " Time:"
						<< (float)(T_end - T_start) / CLOCKS_PER_SEC << "s" << endl;
				}

			}  // end of image_id
		}

		// ��ѡ���Ե�����˫�ߴ����˲�
		void Workspace::selJointBilateralPropagateFilter(const cv::Mat& joint,
			const DepthMap& depthMap,
			const NormalMap& normalMap,
			const float* refK,
			const double sigma_color, const double sigma_space,
			int radius, const int topN,
			DepthMap& outDepthMap, NormalMap& outNormalMap) const
		{
			const int MapWidth = depthMap.GetWidth();
			const int MapHeight = depthMap.GetHeight();

			if (radius <= 0)
			{
				radius = round(sigma_space * 1.5);  // original parameters, ���� sigma_space ���� radius  
			}

			//assert(radius % 2 == 1);  // ȷ�����ڳߴ�������
			const int d = 2 * radius + 1;

			// ԭ����ͼ���ͨ����
			const int channels = joint.channels();

			//float *color_weight = new float[cnj * 256];
			//float *space_weight = new float[d*d];
			//int *space_ofs_row = new int[d*d];  // ����Ĳ�ֵ
			//int *space_ofs_col = new int[d*d];

			vector<float> color_weight(channels * 256);
			vector<float> space_weight(d * d);
			vector<int> space_offsets_row(d * d), space_offsets_col(d * d);

			double gauss_color_coeff = -0.5 / (sigma_color * sigma_color);
			double gauss_space_coeff = -0.5 / (sigma_space * sigma_space);
			// initialize color-related bilateral filter coefficients

			// ɫ��ĸ�˹Ȩ��  
			for (int i = 0; i < 256 * channels; i++)
			{
				color_weight[i] = (float)std::exp(i * i * gauss_color_coeff);
			}

			int MAX_K = 0;   // 0 ~ (2*radius + 1)^2  

			// initialize space-related bilateral filter coefficients  
			//�ռ��ĸ�˹Ȩ��
			// ͳ������������������:���㷽�ε��������Բ������
			for (int i = -radius; i <= radius; i++)
			{
				for (int j = -radius; j <= radius; j++)
				{
					double r = std::sqrt((double)i * i + (double)j * j);

					if (r > radius)
					{
						continue;
					}

					space_weight[MAX_K] = (float)std::exp(r * r * gauss_space_coeff);
					space_offsets_row[MAX_K] = i;
					space_offsets_col[MAX_K++] = j;  // update MAX_K
				}
			}

			//selective joint bilataral propagation filter
			for (int y = 0; y < MapHeight; y++)
			{
				for (int x = 0; x < MapWidth; x++)
				{
					// ���������ֵ(���ֵ����)������
					if (depthMap.GetDepth(y, x) != 0.0f)
					{
						continue;
					}

					// bgr
					const cv::Vec3b color_0 = joint.ptr<cv::Vec3b>(y)[x];

					// ����Ȩ�غ�����λ�õ�����
					vector<pair<float, int>> weightAndIndex;
					weightAndIndex.clear();
					for (int k = 0; k < MAX_K; k++)
					{
						const int yy = y + space_offsets_row[k];
						const int xx = x + space_offsets_col[k];

						// �ж�q, ��ҪqҲ�����ֵ
						if (yy < 0 || yy >= MapHeight || xx < 0
							|| xx >= MapWidth || depthMap.GetDepth(yy, xx) == 0.0f)
						{
							continue;
						}

						//��ɫ����Ȩ�أ��������ڸ߷ֱ���ͼ���ϵ�
						cv::Vec3b color_1 = joint.ptr<cv::Vec3b>(yy)[xx];

						// ����joint��ǰ���غ��������ص� ����Ȩ�� �� ɫ��Ȩ�أ������ۺϵ�Ȩ��
						const float& the_color_weight = color_weight[abs(color_0[0] - color_1[0]) +
							abs(color_0[1] - color_1[1]) + abs(color_0[2] - color_1[2])];
						float w = space_weight[k] * the_color_weight;

						//ֻ����space������ΪȨ��!!!!!!
						//float w = space_weight[k];

						weightAndIndex.push_back(make_pair(w, k));
					}

					// ���Ȩ��ֵΪ��
					if (weightAndIndex.size() == 0)
					{
						continue;
					}
					//if (weightAndIndex.size() < int(0.1f * (float)space_offsets_row.size()))
					//{
					//	continue;
					//}

					//�Դ洢��Ȩ�ؽ��дӴ�С����
					if (topN < weightAndIndex.size())
					{
						partial_sort(weightAndIndex.begin(),
							weightAndIndex.begin() + topN,
							weightAndIndex.end(),
							pairIfDescend);
					}
					else
					{
						sort(weightAndIndex.begin(), weightAndIndex.end(), pairIfDescend);
					}

					//if (weightAndIndex[0].first < 0.3)
					//	continue;

					// ���մӴ�С��Ȩ�أ�������ȴ���
					float sum_w = 0.0f;
					float sum_value_depth = 0.0f;
					float sum_value_normal[3] = { 0.0f };

					const int EffNum = std::min(topN, (int)weightAndIndex.size());
					for (int i = 0; i < EffNum; i++)
					{
						//if (weightAndIndex[i].first < 0.3)
						//	continue;

						int yy = y + space_offsets_row[weightAndIndex[i].second];
						int xx = x + space_offsets_col[weightAndIndex[i].second];

						const float src_depth = depthMap.GetDepth(yy, xx);

						float src_normal[3];
						normalMap.GetSlice(yy, xx, src_normal);

						/****************���ֵ��������****************/

						// ������ȴ���ֵ
						float propagated_depth = PropagateDepth(refK,
							src_depth, src_normal,
							yy, xx, y, x);

						// ��������ֱ����ԭ���ֵ
						//const float propagated_depth = src_depth;

						sum_value_depth += propagated_depth * weightAndIndex[i].first;

						sum_value_normal[0] += src_normal[0] * weightAndIndex[i].first;
						sum_value_normal[1] += src_normal[1] * weightAndIndex[i].first;
						sum_value_normal[2] += src_normal[2] * weightAndIndex[i].first;

						sum_w += weightAndIndex[i].first;
					}

					if (sum_w < 1e-8)
					{
						//cout << "[Warning]: very small sum_w: " << sum_w << endl;
						sum_w += float(1e-8);
						//continue;
					}

					sum_w = 1.0f / sum_w;

					// �������ֵ
					const float out_depth = sum_value_depth * sum_w;

					//// @even DEBUG: to check for Nan dpeth
					//if (isnan(out_depth))
					//{
					//	cout << "\n[Nan out depth]: " << out_depth << endl;
					//}

					outDepthMap.Set(y, x, out_depth);

					// ���÷���ֵ
					sum_value_normal[0] *= sum_w;
					sum_value_normal[1] *= sum_w;
					sum_value_normal[2] *= sum_w;

					// ��������
					SuitNormal(y, x, refK, sum_value_normal);
					outNormalMap.SetSlice(y, x, sum_value_normal);

				}  // end of x
			}  // end of y

		}

		void Workspace::NoiseAwareFilter(const cv::Mat& joint,
			DepthMap& depthMap, const NormalMap& normalMap,
			const float* refK,
			const double& sigma_space, const double& sigma_color, const double& sigma_depth,
			const float& THRESH,
			const float& eps, const float& tau,
			const bool is_propagate,
			int radius,
			DepthMap& outDepthMap, NormalMap& outNormalMap) const
		{
			const int MapWidth = depthMap.GetWidth();
			const int MapHeight = depthMap.GetHeight();

			// original parameters, ���� sigma_space ���� radius 
			if (radius <= 0)
			{
				radius = (int)round(sigma_space * 1.5 + 0.5);
			}

			//assert(radius % 2 == 1);  // ȷ�����ڳߴ�������
			const int d = 2 * radius + 1;

			// ԭ����ͼ���ͨ����
			const int channels = joint.channels();
			const int& color_levels = 256 * channels;

			// ------------ RGBԭͼɫ��, �ռ�����˹Ȩ��
			vector<float> color_weights(color_levels);  
			vector<float> space_weights(d * d);
			vector<int> space_offsets_row(d * d), space_offsets_col(d * d);

			double gauss_color_coeff = -0.5 / (sigma_color * sigma_color);
			double gauss_space_coeff = -0.5 / (sigma_space * sigma_space);
			// initialize color-related bilateral filter coefficients

			// ɫ��ĸ�˹Ȩ��  
			for (int i = 0; i < color_levels; ++i)
			{
				color_weights[i] = (float)std::exp(i * i * gauss_color_coeff);
			}

			int MAX_K = 0;   // 0 ~ (2*radius + 1)^2  

			// initialize space-related bilateral filter coefficients  
			// �ռ��ĸ�˹Ȩ��
			// ͳ��������������������������������Բ����
			for (int i = -radius; i <= radius; ++i)
			{
				for (int j = -radius; j <= radius; ++j)
				{
					const double r = std::sqrt((double)i * i + (double)j * j);
					if (r > radius)
					{
						continue;
					}

					space_weights[MAX_K] = (float)std::exp(r * r * gauss_space_coeff);
					space_offsets_row[MAX_K] = i;
					space_offsets_col[MAX_K++] = j;  // update MAX_K
				}
			}

			//// ����ԭʼ���ͼ��˹ƽ����� 
			//cv::Mat depth_mat, depth_blur;
			//depth_mat = depthMap.Depth2Mat();
			//cv::GaussianBlur(depth_mat, depth_blur, cv::Size(3, 3), 0);

			// ����ÿһ������
			//printf("eps: %.3f, tau: %.3f\n", eps, tau);
			for (int y = 0; y < MapHeight; y++)
			{
				for (int x = 0; x < MapWidth; x++)
				{
					// ���������ֵ(���ֵ����)������
					if (depthMap.GetDepth(y, x) != 0.0f)
					{
						continue;
					}

					//// ����뾶�����ڵ�omega_depth
					//double depth_min = DBL_MAX;
					//double depth_max = -1.0f;
					//for (int k = 0; k < MAX_K; ++k)
					//{
					//	const int yy = y + space_offsets_row[k];
					//	const int xx = x + space_offsets_col[k];

					//	if (yy < 0 || yy >= MapHeight || xx < 0
					//		|| xx >= MapWidth)
					//	{
					//		continue;
					//	}
					//	float depth = depth_blur.at<float>(yy, xx);
					//	if (depth == 0.0f)
					//	{
					//		continue;
					//	}

					//	if (depth > depth_max)
					//	{
					//		depth_max = depth;
					//	}
					//	else if (depth < depth_min)
					//	{
					//		depth_min = depth;
					//	}
					//}

					//// �����������С,������ֵ,û��Ҫ�������ļ���,����������
					//if (depth_min == DBL_MAX || depth_max == -1.0f)
					//{
					//	continue;
					//}

					//const double omega_depth = depth_max - depth_min;

					// p����bgr��ɫֵ
					const cv::Vec3b& color_0 = joint.ptr<cv::Vec3b>(y)[x];

					// p���ص����ֵ
					const double& depth_0 = (double)depthMap.GetDepth(y, x);

					// ͳ��pΪ���ĵ�Բ�δ���, ��Ч��Ȩ�ؼ�������λ�õ�����
					vector<pair<float, int>> WeightAndIndex;
					WeightAndIndex.clear();
					for (int k = 0; k < MAX_K; ++k)
					{
						const int yy = y + space_offsets_row[k];
						const int xx = x + space_offsets_col[k];

						// �ж�q, ��ҪqҲ�����ֵ
						if (yy < 0 || yy >= MapHeight || xx < 0
							|| xx >= MapWidth || depthMap.GetDepth(yy, xx) == 0.0f)
						{
							// ����û�����ֵ��neighbor
							continue;
						}

						// q����bgr��ɫֵ
						cv::Vec3b color_1 = joint.ptr<cv::Vec3b>(yy)[xx];

						// q���ص����ֵ
						const double depth_1 = (double)depthMap.GetDepth(yy, xx);

						// ����ԭʼ���ͼ��Ȳ�ֵ�ĸ�˹����ֵ
						double delta_depth = depth_0 - depth_1;
						const double depth_weight = std::exp(-0.5 * delta_depth * delta_depth
							/ sigma_depth);

						// ����joint��ǰ���غ��������صľ���Ȩ�غ�ɫ��Ȩ�أ������ۺϵ�Ȩ��
						const int delta_color = abs(color_0[0] - color_1[0]) +
							abs(color_0[1] - color_1[1]) + abs(color_0[2] - color_1[2]);
						const float color_weight = color_weights[delta_color];

						// ����Alpha
						//double alpha = depthMap.CalculateAlpha(eps, tau, omega_depth);

						// ���Ǹ���color_weight��depth_weight�����ƶ�ȷ��Alphaֵ....
						const double delta_color_ratio = double(delta_color) / double(color_levels);
						const double delta_depth_ratio = std::abs(delta_depth) / double(depthMap.depth_max_);
						double diff = std::abs(delta_color_ratio - delta_depth_ratio);
						double alpha = std::exp(-0.5 * diff * diff / 0.2);  // to reduce sigma_alpha: 0.2

						const float compound_weight = float(alpha * color_weight + \
							(1.0f - alpha) * depth_weight);

						float weight = space_weights[k] * compound_weight;
						WeightAndIndex.push_back(make_pair(weight, k));
					}

					// ��WeightAndIndex��Size��С���й���
					//if (WeightAndIndex.size() == 0)
					//{
					//	continue;
					//}
					if (WeightAndIndex.size() < size_t(THRESH * (float)space_offsets_row.size()))
					{
						continue;
					}

					// �����Ȩ���ֵ�ͷ�����
					float sum_w = 0.0f, sum_value_depth = 0.0f;
					float sum_value_normal[3] = { 0.0f };
					for (int i = 0; i < (int)WeightAndIndex.size(); ++i)
					{
						int yy = y + space_offsets_row[WeightAndIndex[i].second];
						int xx = x + space_offsets_col[WeightAndIndex[i].second];

						// neighbor q's depth
						const float src_depth = depthMap.GetDepth(yy, xx);

						// neighbor q's normal
						float src_normal[3];
						normalMap.GetSlice(yy, xx, src_normal);

						/****************���ֵ��������****************/
						float depth_val = 0.0f;
						if (is_propagate)
						{
							// ������ȴ���ֵ
							depth_val = PropagateDepth(refK,
								src_depth, src_normal,
								yy, xx, y, x);
						}
						else
						{
							//��������ֱ����ԭ���ֵ
							depth_val = src_depth;
						}
						
						// weighting depth
						sum_value_depth += depth_val * WeightAndIndex[i].first;

						// weighting normal
						sum_value_normal[0] += src_normal[0] * WeightAndIndex[i].first;
						sum_value_normal[1] += src_normal[1] * WeightAndIndex[i].first;
						sum_value_normal[2] += src_normal[2] * WeightAndIndex[i].first;

						sum_w += WeightAndIndex[i].first;
					}

					if (sum_w < 1e-8)
					{
						//cout << "[Warning]: very small sum_w: " << sum_w << endl;
						sum_w += float(1e-8);
						//continue;
					}

					sum_w = 1.0f / sum_w;

					// �������ֵ
					const float out_depth = sum_value_depth * sum_w;
					outDepthMap.Set(y, x, out_depth);

					// ���÷���ֵ
					sum_value_normal[0] *= sum_w;
					sum_value_normal[1] *= sum_w;
					sum_value_normal[2] *= sum_w;

					// ��������
					SuitNormal(y, x, refK, sum_value_normal);
					outNormalMap.SetSlice(y, x, sum_value_normal);
				}
			}
		}

		void Workspace::JTU(const cv::Mat& joint,
			DepthMap& depthMap, const NormalMap& normalMap,
			const float* refK,
			const double& sigma_space, const double& sigma_color, double& sigma_depth,
			const float& THRESH,
			const bool is_propagate,
			int radius,
			DepthMap& outDepthMap, NormalMap& outNormalMap) const
		{
			const int MapWidth = depthMap.GetWidth();
			const int MapHeight = depthMap.GetHeight();

			// original parameters, ���� sigma_space ���� radius 
			if (radius <= 0)
			{
				radius = (int)round(sigma_space * 1.5 + 0.5);
			}

			//assert(radius % 2 == 1);  // ȷ�����ڳߴ�������
			const int d = 2 * radius + 1;

			// ԭ����ͼ���ͨ����
			const int channels = joint.channels();
			const int& color_levels = 256 * channels;

			// ------------ RGBԭͼɫ��, �ռ�����˹Ȩ��
			vector<float> color_weights(color_levels);
			vector<float> space_weights(d * d);
			vector<int> space_offsets_row(d * d), space_offsets_col(d * d);

			double gauss_color_coeff = -0.5 / (sigma_color * sigma_color);
			double gauss_space_coeff = -0.5 / (sigma_space * sigma_space);
			// initialize color-related bilateral filter coefficients

			//// ɫ��ĸ�˹Ȩ��  
			//for (int i = 0; i < color_levels; ++i)
			//{
			//	color_weights[i] = (float)std::exp(i * i * gauss_color_coeff);
			//}

			int MAX_K = 0;   // 0 ~ (2*radius + 1)^2  

			// initialize space-related bilateral filter coefficients  
			// �ռ��ĸ�˹Ȩ��
			// ͳ��������������������������������Բ����
			for (int i = -radius; i <= radius; ++i)
			{
				for (int j = -radius; j <= radius; ++j)
				{
					const double r = std::sqrt((double)i * i + (double)j * j);
					if (r > radius)
					{
						continue;
					}

					//space_weights[MAX_K] = (float)std::exp(r * r * gauss_space_coeff);
					space_offsets_row[MAX_K] = i;
					space_offsets_col[MAX_K++] = j;  // update MAX_K
				}
			}

			// ����ÿһ������
			//printf("eps: %.3f, tau: %.3f\n", eps, tau);
			for (int y = 0; y < MapHeight; y++)
			{
				for (int x = 0; x < MapWidth; x++)
				{
					// ���������ֵ(���ֵ����)������
					if (depthMap.GetDepth(y, x) != 0.0f)
					{
						continue;
					}

					// p����bgr��ɫֵ
					const cv::Vec3b& color_0 = joint.ptr<cv::Vec3b>(y)[x];

					// p���ص����ֵ
					const double depth_0 = (double)depthMap.GetDepth(y, x);

					// ͳ��pΪ���ĵ�Բ�δ���, ��Ч��Ȩ�ؼ�������λ�õ�����
					vector<pair<float, int>> WeightAndIndex;
					WeightAndIndex.clear();
					for (int k = 0; k < MAX_K; ++k)
					{
						const int yy = y + space_offsets_row[k];
						const int xx = x + space_offsets_col[k];

						// �ж�q, ��ҪqҲ�����ֵ
						if (yy < 0 || yy >= MapHeight || xx < 0
							|| xx >= MapWidth || depthMap.GetDepth(yy, xx) == 0.0f)
						{
							// ����û�����ֵ��neighbor
							continue;
						}

						// q����bgr��ɫֵ
						cv::Vec3b color_1 = joint.ptr<cv::Vec3b>(yy)[xx];

						// q���ص����ֵ
						const double depth_1 = (double)depthMap.GetDepth(yy, xx);

						// ����ԭʼ���ͼ��Ȳ�ֵ�ĸ�˹����ֵ
						double delta_depth = depth_0 - depth_1;
						delta_depth /= depthMap.depth_max_;
						if (float(color_0[0] + color_0[1] + color_0[2]) 
							/ float(color_levels) < 0.16f)  // threshold of sigma_depth
						{
							sigma_depth = 0.06;
						}
						const double depth_weight = std::exp(-0.5 * delta_depth * delta_depth
							/ sigma_depth);

						// ����ɫ��Ȩ��
						double delta_color = abs(color_0[0] - color_1[0]) +
							abs(color_0[1] - color_1[1]) + abs(color_0[2] - color_1[2]);
						delta_color /= double(color_levels);
						const double color_weight = std::exp(-0.5 * delta_color * delta_color
							/ sigma_color);
						//const float color_weight = color_weights[delta_color];

						// �������Ȩ��
						//float& space_weight = space_weights[k];
						double delta_space = sqrt((x - xx) * (x - xx) + (y - yy) * (y - yy));
						//delta_space /= double(radius);
						const double space_weight = std::exp(-0.5 * delta_space * delta_space
							/ sigma_space);

						// �����ۺ�Ȩ��
						const float weight = space_weight * color_weight * depth_weight;
						WeightAndIndex.push_back(make_pair(weight, k));
					}

					// ��weightAndIndex��Size��С���й���
					//if (WeightAndIndex.size() == 0)
					//{
					//	continue;
					//}
					if (WeightAndIndex.size() < size_t(THRESH * (float)space_offsets_row.size()))
					{
						continue;
					}

					// �����Ȩ���ֵ�ͷ�����
					float sum_w = 0.0f, sum_value_depth = 0.0f;
					float sum_value_normal[3] = { 0.0f };
					for (int i = 0; i < (int)WeightAndIndex.size(); ++i)
					{
						int yy = y + space_offsets_row[WeightAndIndex[i].second];
						int xx = x + space_offsets_col[WeightAndIndex[i].second];

						// neighbor q's depth
						const float src_depth = depthMap.GetDepth(yy, xx);

						// neighbor q's normal
						float src_normal[3];
						normalMap.GetSlice(yy, xx, src_normal);

						/****************���ֵ��������****************/
						float depth_val = 0.0f;
						if (is_propagate)
						{
							// ������ȴ���ֵ
							depth_val = PropagateDepth(refK,
								src_depth, src_normal,
								yy, xx, y, x);
						}
						else
						{
							//��������ֱ����ԭ���ֵ
							depth_val = src_depth;
						}

						// weighting depth
						sum_value_depth += depth_val * WeightAndIndex[i].first;

						// weighting normal
						sum_value_normal[0] += src_normal[0] * WeightAndIndex[i].first;
						sum_value_normal[1] += src_normal[1] * WeightAndIndex[i].first;
						sum_value_normal[2] += src_normal[2] * WeightAndIndex[i].first;

						sum_w += WeightAndIndex[i].first;
					}

					if (sum_w < 1e-8)
					{
						sum_w += float(1e-8);
					}

					sum_w = 1.0f / sum_w;

					// �������ֵ
					const float out_depth = sum_value_depth * sum_w;
					outDepthMap.Set(y, x, out_depth);

					// ���÷���ֵ
					sum_value_normal[0] *= sum_w;
					sum_value_normal[1] *= sum_w;
					sum_value_normal[2] *= sum_w;

					// ��������
					SuitNormal(y, x, refK, sum_value_normal);
					outNormalMap.SetSlice(y, x, sum_value_normal);
				}
			}
		}

		//�����ͼ�ͷ�����mapͼ��������˫���˲�
		void Workspace::jointBilateralFilter_depth_normal_maps(const cv::Mat& joint,
			const DepthMap& depthMap, const NormalMap& normalMap,
			const float *refK, const double sigma_color, const double sigma_space, int radius,
			DepthMap& outDepthMap, NormalMap& outNormalMap) const
		{
			const int mapWidth = depthMap.GetWidth();
			const int mapHeight = depthMap.GetHeight();

			if (radius <= 0)
				radius = round(sigma_space * 1.5);  // ���� sigma_space ���� radius  

			//assert(radius % 2 == 1);//ȷ�����ڰ뾶������
			const int d = 2 * radius + 1;

			//ԭ����ͼ���ͨ����
			const int cnj = joint.channels();
			vector<float> color_weight(cnj * 256);
			vector<float> space_weight(d*d);
			vector<int> space_ofs_row(d*d), space_ofs_col(d*d);

			double gauss_color_coeff = -0.5 / (sigma_color * sigma_color);
			double gauss_space_coeff = -0.5 / (sigma_space * sigma_space);

			// initialize color-related bilateral filter coefficients  
			// ɫ��ĸ�˹Ȩ��  
			for (int i = 0; i < 256 * cnj; i++)
				color_weight[i] = (float)std::exp(i * i * gauss_color_coeff);

			int maxk = 0;   // 0 - (2*radius + 1)^2  

			// initialize space-related bilateral filter coefficients  
			//�ռ��ĸ�˹Ȩ��
			for (int i = -radius; i <= radius; i++)
			{
				for (int j = -radius; j <= radius; j++)
				{
					double r = std::sqrt((double)i * i + (double)j * j);
					if (r > radius)
						continue;
					space_weight[maxk] = (float)std::exp(r * r * gauss_space_coeff);
					space_ofs_row[maxk] = i;
					space_ofs_col[maxk++] = j;
				}
			}

			//selective joint bilataral propagation filter
			for (int r = 0; r < mapHeight; r++)
			{
				for (int l = 0; l < mapWidth; l++)
				{
					if (depthMap.GetDepth(r, l) != 0.0f)//�������ȵ��ˣ�������
						continue;

					const cv::Vec3b color0 = joint.ptr<cv::Vec3b>(r)[l];
					float sum_w = 0;
					float sum_value_depth = 0;
					float sum_value_normal[3] = { 0.0f };
					for (int k = 0; k < maxk; k++)
					{
						const int rr = r + space_ofs_row[k];
						const int ll = l + space_ofs_col[k];

						if (rr < 0 || rr >= mapHeight || ll < 0
							|| ll >= mapWidth || depthMap.GetDepth(rr, ll) == 0)
							continue;

						//��ɫ����Ȩ�أ��������ڸ߷ֱ���ͼ���ϵ�
						cv::Vec3b color1 = joint.ptr<cv::Vec3b>(rr)[ll];

						//// ����joint��ǰ���غ��������ص� ����Ȩ�� �� ɫ��Ȩ�أ������ۺϵ�Ȩ��  
						float w = space_weight[k] * color_weight[abs(color0[0] - color1[0]) +
							abs(color0[1] - color1[1]) + abs(color0[2] - color1[2])];

						const float srcDepth = depthMap.GetDepth(rr, ll);
						float srcNormal[3]; normalMap.GetSlice(rr, ll, srcNormal);

						sum_value_depth += srcDepth * w;
						sum_value_normal[0] += srcNormal[0] * w;
						sum_value_normal[1] += srcNormal[1] * w;
						sum_value_normal[2] += srcNormal[2] * w;
						sum_w += w;
					}
					if (sum_w == 0)
						continue;

					sum_w = 1 / sum_w;
					outDepthMap.Set(r, l, sum_value_depth*sum_w);
					sum_value_normal[0] *= sum_w;
					sum_value_normal[1] *= sum_w;
					sum_value_normal[2] *= sum_w;
					SuitNormal(r, l, refK, sum_value_normal);
					outNormalMap.SetSlice(r, l, sum_value_normal);
				}//end of l
			}//end of r

		}

		//ֻ���þ���Ȩ�ؽ����˲���ֵ
		void Workspace::distanceWeightFilter(const DepthMap &depthMap, const NormalMap &normalMap,
			const float *refK, const double sigma_color, const double sigma_space, int radius,
			DepthMap &outDepthMap, NormalMap &outNormalMap) const
		{
			const int mapWidth = depthMap.GetWidth();
			const int mapHeight = depthMap.GetHeight();

			if (radius <= 0)
				radius = round(sigma_space * 1.5);  // ���� sigma_space ���� radius  

			//assert(radius % 2 == 1);//ȷ�����ڰ뾶������
			const int d = 2 * radius + 1;

			//ԭ����ͼ���ͨ����
			vector<float> space_weight(d*d);
			vector<int> space_ofs_row(d*d), space_ofs_col(d*d);
			double gauss_space_coeff = -0.5 / (sigma_space * sigma_space);

			int maxk = 0;   // 0 - (2*radius + 1)^2  

			// initialize space-related bilateral filter coefficients  
			//�ռ��ĸ�˹Ȩ��
			for (int i = -radius; i <= radius; i++)
			{
				for (int j = -radius; j <= radius; j++)
				{
					double r = std::sqrt((double)i * i + (double)j * j);
					if (r > radius)
						continue;
					space_weight[maxk] = (float)std::exp(r * r * gauss_space_coeff);
					space_ofs_row[maxk] = i;
					space_ofs_col[maxk++] = j;
				}
			}

			//use sapce distance wight only
			for (int r = 0; r < mapHeight; r++)
			{
				for (int l = 0; l < mapWidth; l++)
				{
					if (depthMap.GetDepth(r, l) != 0.0f)  // �������ȵ��ˣ�������
						continue;

					float sum_w = 0;
					float sum_value_depth = 0;
					float sum_value_normal[3] = { 0.0f };
					for (int k = 0; k < maxk; k++)
					{
						const int rr = r + space_ofs_row[k];
						const int ll = l + space_ofs_col[k];

						if (rr < 0 || rr >= mapHeight || ll < 0 || ll >= mapWidth || depthMap.GetDepth(rr, ll) == 0)
							continue;

						// ����Ȩ��   
						float w = space_weight[k];

						const float srcDepth = depthMap.GetDepth(rr, ll);
						float srcNormal[3]; normalMap.GetSlice(rr, ll, srcNormal);

						sum_value_depth += srcDepth * w;
						sum_value_normal[0] += srcNormal[0] * w;
						sum_value_normal[1] += srcNormal[1] * w;
						sum_value_normal[2] += srcNormal[2] * w;
						sum_w += w;
					}
					if (sum_w == 0)
						continue;

					sum_w = 1 / sum_w;
					outDepthMap.Set(r, l, sum_value_depth*sum_w);
					sum_value_normal[0] *= sum_w;
					sum_value_normal[1] *= sum_w;
					sum_value_normal[2] *= sum_w;
					SuitNormal(r, l, refK, sum_value_normal);
					outNormalMap.SetSlice(r, l, sum_value_normal);
				}//end of l
			}//end of r
		}

	}  // namespace mvs	
}  // namespace colmap


					// @even: ����lapulace��Ե, �������ͼ������˫���˲�
					//cv::Mat blured, guide;
					//cv::blur(src_img, blured, Size(9, 9));
					//cv::Laplacian(blured, guide, -1, 9);
					//selJointBilataralPropagateFilter(guide,
					//	m_depth_maps.at(img_id),
					//	m_normal_maps.at(img_id),
					//	model_.m_images.at(img_id).GetK(),
					//	25, 10,  // 25, 10
					//	-1, 16,
					//	depthMap_pro, normalMap_pro);

					// @even: ��selJB֮������ͼ��������˫���˲�
					//cv::Mat dst, mat = depthMap_pro.Depth2Mat();
					//src_img.convertTo(src_img, CV_32FC1);
					//int k = 10;
					//cv::ximgproc::jointBilateralFilter(src_img, mat, dst,
					//-1, 2 * k - 1, 2 * k - 1);
					//depthMap_pro.fillDepthWithMat(dst);

					// @even: ��selJB֮������ͼ���������˲�
					//cv::Mat dst, mat = depthMap_pro.Depth2Mat();
					//double eps = 1e-6;
					//eps *= 255.0 * 255.0;
					//dst = guidedFilter(src_img, mat, 10, eps);
					//depthMap_pro.fillDepthWithMat(dst);

						// ����˫���˲�
						//jointBilateralFilter_depth_normal_maps(srcImage, depthMaps_.at(image_id), normalMaps_.at(image_id),
						//	model_.images.at(image_id).GetK(), 25, 10, -1, depthMap_pro, normalMap_pro);

						// ֻ���ÿռ����Ȩ�ز�ֵ
						//distanceWeightFilter(depthMaps_.at(image_id), normalMaps_.at(image_id),
						//	model_.images.at(image_id).GetK(), 25, 10, -1, depthMap_pro, normalMap_pro);