#define BOOST_TEST_MODULE Core_Convolution
#include <boost/test/unit_test.hpp>
#include <boost/test/floating_point_comparison.hpp>

#include <shark/Core/Images/Convolution.h>

using namespace shark;
using namespace std;
using namespace shark::blas;
template<class E1, class E2, class M>
void convolutionReference(
	matrix_expression<E1, cpu_tag> const& image,
	matrix_expression<E2, cpu_tag> const& filter,
	matrix_expression<M, cpu_tag>& output,
	std::size_t num_channels,
	std::size_t filter_size1,
	std::size_t filter_size2,
	std::size_t num_filters
){
	std::size_t output_size1 = output().size1() / num_filters;
	std::size_t output_size2 = output().size2();
	std::size_t image_size1 = image().size1()/num_channels;
	
	output().clear();
	
	for(std::size_t f = 0; f != num_filters; ++f){
		std::size_t start_out = f * output_size1;
		for(std::size_t c = 0; c != num_channels; ++c){
			std::size_t start_filter = c * filter_size1 * filter_size2;
			std::size_t start_image = c * image_size1;			
			for(std::size_t i = 0; i != output_size1; ++i){
				for(std::size_t j = 0; j != output_size2; ++j){
					for(std::size_t i0 = 0; i0 != filter_size1; ++i0){
						for(std::size_t j0 = 0; j0 != filter_size2; ++j0){
							output()(start_out + i,j) += image()(start_image + i + i0,j + j0) * filter()(f, start_filter + i0 * filter_size2 + j0); 
						}
					}
				}
			}
		}
	}
}

template<class T>
matrix<T> padZero(matrix<T> const& inputs, 
	std::size_t num_channels,
	std::size_t image_size1, std::size_t image_size2,
	std::size_t pad_height, std::size_t pad_width,
	bool flipped
){
	std::size_t padded1 = image_size1+pad_height;
	std::size_t padded2 = image_size2+pad_width;
	std::size_t top = pad_height/2;
	std::size_t bot = pad_height - top;
	std::size_t left = pad_width/2;
	std::size_t right =pad_width - left;
	if(flipped){
		std::swap(top, bot);
		std::swap(left, right);
	}
	//convert to subrange coordinates
	bot = image_size1 + top;
	right = image_size2 + left;
	matrix<T> outputs(num_channels * padded1, padded2);
	for(std::size_t c = 0; c != num_channels; ++c){
		auto channelIn = rows(inputs, c*image_size1, (c+1)*image_size1);
		auto channelOut = rows(outputs, c*padded1, (c+1)*padded1);
		
		noalias(subrange(channelOut, top,bot,left,right)) = channelIn;
	}
	return outputs;
}

template<class T, class Device = cpu_tag>
matrix<T> flipFilter(matrix<T> const& inputs, 
	std::size_t num_channels,
	std::size_t filter_size1, std::size_t filter_size2
){
	matrix<T> outputs(inputs.size1(), num_channels *  filter_size1 * filter_size2);
	
	for(std::size_t f = 0; f != inputs.size1(); ++f){
		for(std::size_t c = 0; c != num_channels; ++c){
			for(std::size_t i = 0; i !=  filter_size1; ++i){
				for(std::size_t j = 0; j != filter_size2; ++j){
					std::size_t iFlipped = filter_size1 - i -1;
					std::size_t jFlipped = filter_size2 - j -1;
					outputs(f, (c * filter_size1 + iFlipped) * filter_size2 + jFlipped)  = inputs(f, (c * filter_size1 + i) * filter_size2 + j);
				}
			}
		}
	}
	return outputs;
}

template<class T>
matrix<T> toCNHW(matrix<T> const& inputs, 
	std::size_t num_channels,
	std::size_t filter_size1, std::size_t filter_size2
){
	matrix<T> outputs(num_channels, inputs.size1()  *  filter_size1 * filter_size2);
	
	for(std::size_t f = 0; f != inputs.size1(); ++f){
		for(std::size_t c = 0; c != num_channels; ++c){
			for(std::size_t i = 0; i !=  filter_size1; ++i){
				for(std::size_t j = 0; j != filter_size2; ++j){
					outputs(c, (f * filter_size1 + i) * filter_size2 + j)  = inputs(f, (c * filter_size1 + i) * filter_size2 + j);
				}
			}
		}
	}
	return outputs;
}

template<class T, class Device=cpu_tag>
void test(
	std::size_t image_size1, std::size_t image_size2,
	std::size_t filter_size1, std::size_t filter_size2,
	std::size_t num_channels,
	std::size_t num_filters,
	std::size_t num_images,
	bool pad,
	ImageFormat imageFormat,
	ImageFormat filterFormat,
	bool flipped= false
){
	//create filter
	matrix<T> filter_cpu(num_filters, num_channels *  filter_size1 * filter_size2);
	for(std::size_t f = 0; f != num_filters; ++f){
		for(std::size_t c = 0; c != num_channels; ++c){
			for(std::size_t i = 0; i !=  filter_size1; ++i){
				for(std::size_t j = 0; j != filter_size2; ++j){
					double val = (1.0/filter_size1)*i + 0.1 - (0.1/filter_size2)*j+(0.1*f)/num_filters-(0.1*c)/num_channels;
					filter_cpu(f, (c * filter_size1 + i) * filter_size2 + j)  = val;
				}
			}
		}
	}
	
	//create the transformed filter for ground truth
	matrix<T> filter_transformed = filter_cpu;
	if(flipped){
		filter_transformed = flipFilter(filter_transformed, num_channels, filter_size1, filter_size2);
	}

	std::size_t pad_height = pad? filter_size1-1: 0;
	std::size_t pad_width = pad? filter_size2-1: 0;
	std::size_t output_size1 = image_size1 - filter_size1 + 1 + pad_height;
	std::size_t output_size2 = image_size2 - filter_size2 + 1 + pad_width;
	matrix<T> images_cpu(num_images, num_channels * image_size1 *  image_size2);
	
	matrix<T> outputs_test(num_images, output_size1 * output_size2 * num_filters, 0.0);
	//create images and ground truth
	for(std::size_t im = 0; im != num_images; ++im){
		matrix<T> image(num_channels * image_size1,image_size2,0.0);
		for(std::size_t c = 0; c != num_channels; ++c){
			for(std::size_t i = 0; i !=  image_size1; ++i){
				for(std::size_t j = 0; j != image_size2; ++j){
					image(c*image_size1 + i,j)  = 1.0/image_size1*i + 0.1 - (0.1/image_size2)*j + (0.1*c)/num_channels;
				}
			}
		}
		//for the ground truth make padding explicit
		matrix<T> image_truth = padZero(image, num_channels, image_size1, image_size2, pad_height,pad_width, flipped);
		//compute result
		matrix<T> out(output_size1 * num_filters, output_size2, 0.0);
		convolutionReference(image_truth,filter_transformed,out,num_channels, filter_size1, filter_size2, num_filters);
		
		//store image for processing
		noalias(row(images_cpu,im)) = to_vector(image);
		noalias(row(outputs_test,im)) = to_vector(out);
	}
	
	//reorder the filter to CNHW format if needed
	std::size_t firstFilterCoord = num_channels;
	if(filterFormat == ImageFormat::CNHW){
		filter_cpu = toCNHW(filter_cpu, num_channels, filter_size1, filter_size2);
		firstFilterCoord = num_filters;
	}
	
	//reorder images to CNHW format if needed
	std::size_t firstImageCoord = num_channels;
	std::size_t firstOutputCoord = num_filters;
	if(imageFormat == ImageFormat::CNHW){
		images_cpu = toCNHW(images_cpu, num_channels, image_size1, image_size2);
		outputs_test = toCNHW(outputs_test, num_filters, output_size1, output_size2);
		firstImageCoord = num_images;
		firstOutputCoord = num_channels;
	}
	
	
	//run the algorithm
	matrix<T, row_major, Device> images = copy_to_device(images_cpu, Device());
	matrix<T,row_major, Device> filter = copy_to_device(filter_cpu, Device());
	matrix<T, row_major, Device> outputs(outputs_test.size1(), outputs_test.size2(), 0.0);
	image::convolution(
		images,filter,outputs,
		{firstImageCoord, image_size1, image_size2},
		{firstFilterCoord, filter_size1, filter_size2},
		{firstOutputCoord,output_size1, output_size2},
		pad_height,pad_width,
		imageFormat, filterFormat, flipped
	);
	matrix<T> outputs_cpu = copy_to_cpu(outputs);

	//~ std::cout<<outputs_cpu<<std::endl;
	//~ std::cout<<outputs_test<<std::endl;
	for(std::size_t im = 0; im != outputs.size1(); ++im){
		for(std::size_t k = 0; k != outputs.size2(); ++k){
			BOOST_CHECK_CLOSE(outputs_cpu(im, k),outputs_test(im, k),1.e-2);
		}
	}
}

BOOST_AUTO_TEST_SUITE(Image_Convolution)

BOOST_AUTO_TEST_CASE(Convolution_Basic_NCHW_Test) {
	//very simple tests designed to catch indexing errors
	test<float>(6,6,4,4,1,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,2,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,1,2,1, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,1,1,2, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,2,2,2, false, ImageFormat::NCHW, ImageFormat::NCHW);
	
	//real tests
	test<float>(32,16,4,8,5,7,4, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(16,12,4,8,4,4,3, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(57,33,7,3,22,15,3, false, ImageFormat::NCHW, ImageFormat::NCHW);
}

BOOST_AUTO_TEST_CASE(Convolution_NCHW_pad_Test) {
	//very simple tests designed to catch indexing errors
	test<float>(6,6,4,4,1,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,2,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,1,2,1, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,1,1,2, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,2,2,2, true, ImageFormat::NCHW, ImageFormat::NCHW);
	
	//real tests
	test<float>(32,16,4,8,5,7,4, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(16,12,4,8,4,4,3, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float>(57,33,7,3,22,15,3, true, ImageFormat::NCHW, ImageFormat::NCHW);
}

BOOST_AUTO_TEST_CASE(Convolution_Basic_NCHW_flip_pad_Test) {
	//very simple tests designed to catch indexing errors
	test<float>(6,6,4,4,1,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,2,1, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,1,2, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,2,2, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	
	//real tests
	test<float>(32,16,4,8,5,7,4, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(16,12,4,8,4,4,3, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(57,33,7,3,22,15,3, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	
	//flip and pad
	test<float>(6,6,4,4,1,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,2,1, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,1,2, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,2,2, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
}

BOOST_AUTO_TEST_CASE(Convolution_Basic_CNHW_NCHW_Test) {
	//very simple tests designed to catch indexing errors
	test<float>(6,6,4,4,1,1,1, false, ImageFormat::CNHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,2,1,1, false, ImageFormat::CNHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,1,2,1, false, ImageFormat::CNHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,1,1,2, false, ImageFormat::CNHW, ImageFormat::NCHW);
	test<float>(6,6,4,4,2,2,2, false, ImageFormat::CNHW, ImageFormat::NCHW);
	
	//real tests
	test<float>(32,16,4,8,5,7,4, false, ImageFormat::CNHW, ImageFormat::NCHW);
	test<float>(16,12,4,8,4,4,3, false, ImageFormat::CNHW, ImageFormat::NCHW);
	test<float>(57,33,7,3,22,15,3, false, ImageFormat::CNHW, ImageFormat::NCHW);
	
	//flip
	test<float>(6,6,4,4,1,1,1, false, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,1,1, false, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,2,1, false, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,1,2, false, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,2,2, false, ImageFormat::CNHW, ImageFormat::NCHW, true);
	
	//flip + pad
	test<float>(6,6,4,4,1,1,1, true, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,1,1, true, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,2,1, true, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,1,1,2, true, ImageFormat::CNHW, ImageFormat::NCHW, true);
	test<float>(6,6,4,4,2,2,2, true, ImageFormat::CNHW, ImageFormat::NCHW, true);
}

BOOST_AUTO_TEST_CASE(Convolution_Basic_NCHW_CNHW_Test) {
	//very simple tests designed to catch indexing errors
	test<float>(6,6,4,4,1,1,1, false, ImageFormat::NCHW, ImageFormat::CNHW);
	test<float>(6,6,4,4,2,1,1, false, ImageFormat::NCHW, ImageFormat::CNHW);
	test<float>(6,6,4,4,1,2,1, false, ImageFormat::NCHW, ImageFormat::CNHW);
	test<float>(6,6,4,4,1,1,2, false, ImageFormat::NCHW, ImageFormat::CNHW);
	test<float>(6,6,4,4,2,2,2, false, ImageFormat::NCHW, ImageFormat::CNHW);
	
	//real tests
	test<float>(32,16,4,8,5,7,4, false, ImageFormat::NCHW, ImageFormat::CNHW);
	test<float>(16,12,4,8,4,4,3, false, ImageFormat::NCHW, ImageFormat::CNHW);
	test<float>(57,33,7,3,22,15,3, false, ImageFormat::NCHW, ImageFormat::CNHW);
	
	//flip
	test<float>(6,6,4,4,1,1,1, false, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,2,1,1, false, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,1,2,1, false, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,1,1,2, false, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,2,2,2, false, ImageFormat::NCHW, ImageFormat::CNHW, true);
	
	//flip + pad
	test<float>(6,6,4,4,1,1,1, true, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,2,1,1, true, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,1,2,1, true, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,1,1,2, true, ImageFormat::NCHW, ImageFormat::CNHW, true);
	test<float>(6,6,4,4,2,2,2, true, ImageFormat::NCHW, ImageFormat::CNHW, true);
}

#if defined(__HCC__) || defined(__NVCC__)
BOOST_AUTO_TEST_CASE(HIP_Convolution_Basic_NCHW_Test) {
	//very simple tests designed to catch indexing errors
	test<float, hip_tag>(6,6,4,4,1,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,4,4,2,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,4,4,1,2,1, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,4,4,1,1,2, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,4,4,2,2,2, false, ImageFormat::NCHW, ImageFormat::NCHW);
	
	//real tests
	test<float, hip_tag>(32,16,4,8,5,7,4, false, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(16,12,4,8,4,4,3, false, ImageFormat::NCHW, ImageFormat::NCHW);
}

BOOST_AUTO_TEST_CASE(HIP_Convolution_NCHW_pad_Test) {
	//very simple tests designed to catch indexing errors
	test<float, hip_tag>(6,6,3,3,1,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,3,3,2,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,3,3,1,2,1, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,3,3,1,1,2, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(6,6,3,3,2,2,2, true, ImageFormat::NCHW, ImageFormat::NCHW);
	
	//real tests
	test<float, hip_tag>(32,16,3,7,5,7,4, true, ImageFormat::NCHW, ImageFormat::NCHW);
	test<float, hip_tag>(16,12,3,7,4,4,3, true, ImageFormat::NCHW, ImageFormat::NCHW);
}

BOOST_AUTO_TEST_CASE(HIP_Convolution_Basic_NCHW_flip_pad_Test) {
	//very simple tests designed to catch indexing errors
	test<float, hip_tag>(6,6,3,3,1,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,2,1,1, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,1,2,1, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,1,1,2, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,2,2,2, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	
	//real tests
	test<float, hip_tag>(32,16,3,7,5,7,4, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(16,12,3,7,4,4,3, false, ImageFormat::NCHW, ImageFormat::NCHW, true);
	
	//flip and pad
	test<float, hip_tag>(6,6,3,3,1,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,2,1,1, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,1,2,1, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,1,1,2, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
	test<float, hip_tag>(6,6,3,3,2,2,2, true, ImageFormat::NCHW, ImageFormat::NCHW, true);
}
#endif


BOOST_AUTO_TEST_SUITE_END()