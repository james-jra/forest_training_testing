#include "DataPointCollection.h"

namespace MicrosoftResearch { namespace Cambridge { namespace Sherwood
{
    // Iterares through a depth image (16 bit uint) and classifies each pixel 
    // into correct depth bin as specified in the pix_to_label look-up table
    cv::Mat createLabelMatrix(cv::Mat depth_image, 
        std::vector<int> pix_to_label)
    {
        cv::Size mat_size = depth_image.size();
        cv::Mat label_mat(mat_size, CV_8UC1);
        int max = pix_to_label.size() - 1;
        // iterate through pixels in depth image, bin them and assign the depth 
        // label
        for (int r = 0; r < mat_size.height; r++)
        {
            uint16_t* d_pixel = depth_image.ptr<uint16_t>(r);
            uchar* label_pixel = label_mat.ptr<uchar>(r);
            for (int c = 0; c < mat_size.width; c++)
            {
                if (d_pixel[c] <= max)
                    label_pixel[c] = pix_to_label[d_pixel[c]];
                else
                    label_pixel[c] = 0;
            }
        }

        return label_mat;
    }

    // Load up some images from path specified in the program parameters
    // If it's a classifiaction forest, or a full-spread regressor, and we're 
    // training on a zero IR input, we don't need to keep an index of all 
    // pixel locations, they can be calculated on-the-fly. This is referred to
    // as the "low memory" implementation in this case.
    std::unique_ptr<DataPointCollection> DataPointCollection::LoadImages(
        ProgramParameters& progParams, bool classification, int class_number)
    {
        std::string prefix = progParams.InputPrefix;
        cv::Size img_size = cv::Size(progParams.ImgWidth, progParams.ImgHeight);
        
        // for shorthand
        std::string path = progParams.TrainingImagesPath;
        if (!IPUtils::dirExists(path))
            throw std::runtime_error("Failed to find directory:\t" + path);

        if (progParams.PatchSize % 2 == 0)
            throw std::runtime_error("Patch size must be odd");
        
        int number = progParams.NumberTrainingImages;
        int first = progParams.TrainingImagesStart;
        int last = first + number -1;

        // Set up DataPointCollection object
        std::unique_ptr<DataPointCollection> result = std::unique_ptr<DataPointCollection>(new DataPointCollection());
        
        // the result dimension here is interpreted differently depending on
        // the split function... it's bad design but it comes back to the 
        // Sherwood "interfaces".
        if(progParams.SplitFunctionType == SplitFunctionDescriptor::PixelDifference)
            result->dimension_ = progParams.PatchSize * progParams.PatchSize;
        else
            result->dimension_ = progParams.PatchSize;

        result->depth_raw = progParams.DepthRaw;
        result->image_size = img_size;
        result->step = img_size.height * img_size.width;
        result->data_vec_size = (uint32_t)(number * img_size.height * img_size.width);
        // For classification, always use the low memory implementation because there's no need
        // to filter by bins.
        result->train_on_zero = progParams.TrainOnZeroIR;
        if(progParams.TrainOnZeroIR && (classification||class_number == -1))
        {
            result->low_memory = true;
        }
        else
        {
            result->low_memory = false;
        }

        // If it's not a low memory implementation, we need a data vector
        // to hold the valid pixel indices.
        if(!(result->low_memory))
        {
            result->data_.resize(result->data_vec_size);
        }

        // Save integer labels for classifier, target values for regressor
        if(classification) 
        {
            result->labels_.resize(result->data_vec_size);
        }
        else
        {
            result->targets_.resize(result->data_vec_size);
        }

        result->images_.resize(number);
        int img_no = 0;
        int label_no = 0;
        int target_no = 0;
        int datum_no = 0;

        // Variables affecting class formation
        bool zero_class = true;
        int total_classes = progParams.Bins;
        int max_range = progParams.MR;
        // This max parameter is important. Don't forget this is aimed at 16 bit unsigned ints, 
        // so the viable range is 0-65535. 
        // If not using RAW depth data format, it's measured in mm, so be sensible (i.e. 1000 - 1500 mm?)
        int max = progParams.DepthRaw ? 65000 : max_range;
        result->pixelLabels_ = IPUtils::generateDepthBinMap(true, total_classes, max);
        cv::Mat ir_image, ir_preprocessed, depth_image, depth_labels;

        std::string ir_path;
        std::string depth_path;
        cv::Size ir_size, depth_size;

        std::string ir_path_suffix = progParams.Webcam? "cam.png" : "ir.png";

        for (int i = first; i <= last;i++)
        {
            // generate individual image paths
            ir_path = path + prefix + std::to_string(i) + ir_path_suffix;
            depth_path = path + prefix + std::to_string(i) + "depth.png";

            //std::cout << std::to_string(i) << std::endl;
            // read depth and ir images
            ir_image = cv::imread(ir_path, -1);
            depth_image = cv::imread(depth_path, -1);

            // if program fails to open image
            if(!ir_image.data)
            {
                std::cerr << "Failed to open image:\n\t" + ir_path << std::endl;
                continue;
            }
            if (!depth_image.data)
            {
                std::cerr << "Failed to open image:\n\t" + depth_path << std::endl;
                continue;
            }
            // If the datatypes in the images are incorrect
            if (IPUtils::getTypeString(ir_image.type()) != "8UC1")
                throw std::runtime_error("Encountered image with unexpected content type:\n\t" + ir_path);

            if (IPUtils::getTypeString(depth_image.type()) != "16UC1")
                throw std::runtime_error("Encountered image with unexpected content type:\n\t" + depth_path);

            ir_size = ir_image.size();
            depth_size = depth_image.size();
            if (ir_size != depth_size)
                throw std::runtime_error("Depth and IR images not the same size:\n\t" + ir_path + depth_path);

            // Create matrix of depth labels (ie depth bins)
            depth_labels = createLabelMatrix(depth_image, result->pixelLabels_);
            if(!progParams.Closeup)
            {
                int tallest_bin = IPUtils::getTallestBin(depth_labels);
                std::cout << std::to_string(tallest_bin) << std::endl;
                if(tallest_bin == 1)
                {
                    continue;
                }
            }

            // Send the ir image for preprocessing, default values used for now
            ir_preprocessed = IPUtils::preProcess(ir_image, progParams.Threshold);
            result->images_[img_no] = ir_preprocessed;

            
            // iterate through depth_labels matrix and add each element
            // to results.
            // Separated classification and regression out to try and
            // reduce the number of if else clauses insize big for loops
            if(classification)
            {
                for (int r = 0; r < depth_size.height; r++)
                {
                    uchar* label_pixel = depth_labels.ptr<uchar>(r);
                    uchar* ir_pixel = ir_preprocessed.ptr<uchar>(r);
                    for (int c = 0; c < depth_size.width; c++)
                    {
                        if(result->train_on_zero)
                        {
                            result->labels_[label_no] = label_pixel[c];
                            label_no++;
                        }
                        else
                        {
                            if(ir_pixel[c] != 0)
                            {
                                result->labels_[label_no] = label_pixel[c];
                                label_no++;       
                                result->data_[datum_no] = (img_no * result->step) + (r * result->image_size.width) + c;
                                datum_no++;
                            }
                        }
                    }
                }
            }
            else
            {
                // Regression, iterate through pixels, 
                // If pixel's in correct depth bin, add to targets vector
                // If not training on zero IR, then skip zero IR values
                // If not using low memory, add index to data vector
                for (int r = 0; r < depth_size.height; r++)
                {
                    uint16_t* depth_pixel = depth_image.ptr<uint16_t>(r);
                    uchar* label_pixel = depth_labels.ptr<uchar>(r);
                    uchar* ir_pixel = ir_preprocessed.ptr<uchar>(r);
                    for (int c = 0; c < depth_size.width; c++)
                    {
                        if (class_number == label_pixel[c] || class_number == -1)
                        {
                            // if training on zero, always add depth target, 
                            // Add index if not low memory implementation
                            // If not training on zero IR input, add to targets
                            // and also add index to data vector.
                            if(result->train_on_zero)
                            {
                                result->targets_[target_no] = depth_pixel[c];
                                target_no++;
                                if(!(result->low_memory))
                                {
                                    result->data_[datum_no] = (img_no * result->step) + (r * result->image_size.width) + c;
                                    datum_no++;
                                }
                            }
                            else
                            {
                                if(ir_pixel[c] != 0)
                                {
                                    result->targets_[target_no] = depth_pixel[c];
                                    target_no++;
                                    result->data_[datum_no] = (img_no * result->step) + (r * result->image_size.width) + c;
                                    datum_no++;                               
                                }
                            }
                        }
                    }
                }
            }
            img_no++;
        }

        if((!result->low_memory) || (!progParams.Closeup))
        {   
            // Resize data and targets vector to however full they are.
            // Shrink to fit new size to free up excess memory.
            result->data_.resize(datum_no);
            result->data_.shrink_to_fit();
            result->labels_.resize(label_no);
            result->labels_.shrink_to_fit();
            result->targets_.resize(target_no);
            result->targets_.shrink_to_fit();
            result->images_.resize(img_no);
            result->images_.shrink_to_fit();
        }
        
        return result;
    }

    // Load up a single cv:Mat object as a DataPointCollection
    std::unique_ptr<DataPointCollection> DataPointCollection::LoadMat(cv::Mat mat_in, cv::Size img_size, bool inc_zero , bool pre_process, int pp_value)
    {
        // If the datatypes in the images are incorrect
        if (IPUtils::getTypeString(mat_in.type()) != "8UC1")
            throw std::runtime_error("Incorrect image type, expecting CV_8UC1");

        // Set up DataPointCollection object
        std::unique_ptr<DataPointCollection> result = std::unique_ptr<DataPointCollection>(new DataPointCollection());
        result->dimension_ = 1;
        result->image_size = img_size;
        result->images_.resize(1);
        result->step = img_size.height * img_size.width;
        
        // Send the ir image for preprocessing
        if(pre_process)
        {
            result->images_[0] = IPUtils::preProcess(mat_in, pp_value);
        }
        else 
        {
            result->images_[0] = mat_in;
        }

        if(inc_zero)
        {
            result->low_memory = true;
            result->data_vec_size = img_size.height * img_size.width;
        }
        else
        {
            result->low_memory = false;
            int rows = img_size.height;
            int cols = img_size.width;
            int datum_no = 0;
            result->data_.resize(rows*cols);
            for(int r=0;r<rows;r++)
            {
                uchar* ir_ptr = result->images_[0].ptr<uchar>(r);
                for(int c=0;c<cols;c++)
                {
                    if(ir_ptr[c] == 0)
                    {
                        continue;
                    }
                    else
                    {
                        result->data_[datum_no] = (r * cols) + c;
                        datum_no++;
                    }
                }
            }
            result->data_.resize(datum_no);
            result->data_.shrink_to_fit();
        }
        

        return result;
    }

}   }   }
