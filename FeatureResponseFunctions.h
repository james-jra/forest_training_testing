#pragma once

// This file defines some IFeatureResponse implementations used by the example code in
// Classification.h, DensityEstimation.h, etc. Note we represent IFeatureResponse
// instances using simple structs so that all tree data can be stored
// contiguously in a linear array.

#include <string>
#include <math.h>
#include <vector>
#include <cmath>
#include <sstream>

#include <opencv2/opencv.hpp>

#include "DataPointCollection.h"
#include "Interfaces.h"
#include "Random.h"


namespace MicrosoftResearch {
    namespace Cambridge {
        namespace Sherwood
        {
            class Random;
            
            // Generate a normal distributed number given a uniform random number generator
            // Box-muller transform.
            static float randn(Random& random)
            {
                float u = (2 * random.NextDouble()) - 1;
                float v = (2 * random.NextDouble()) - 1;
                float w = u * u + v * v;

                if (w == 0 || w > 1) 
                    return randn(random);

                float x = sqrt(-2 * log(w) / w);
                return u * x;
            }

            /// <summary>   
            ///     f(x) = Sum(p(x)) where n is a randomly generated integer,
            ///     and p(x) is a pixel in the patch surrounding pixel x. patch size^2 = dimensions
            /// </summary>
            class RandomHyperplaneFeatureResponse
            {
            public:
                std::vector<cv::Point> offset;
                unsigned dimensions;

                RandomHyperplaneFeatureResponse() {
                    dimensions = 0;
                }

                /// <summary>
                /// Creates a RandomHyperplaneFeatureResponse object.
                /// The randomly generated variables here are the offset values
                /// </summary>
                RandomHyperplaneFeatureResponse(Random& random,
                    unsigned int dimensions)
                    : dimensions(dimensions)
                {
                    offset.resize(dimensions);

                    int ub = (int)((sqrt(dimensions) -1) / 2);
                    int lb = 0 - ub;
                    // Normal distributed numbers to gives an unbiased random unit vector.
                    for (unsigned int c = 0; c < dimensions; c++) {
                        offset[c] = cv::Point(random.Next(lb,ub), random.Next(lb,ub));
                    }
                }

                static RandomHyperplaneFeatureResponse CreateRandom(Random& random, unsigned int dimensions);

                // IFeatureResponse implementation
                /// <summary>
                /// Calculates the sum of a number of pixels in a patch surrounding a pixel
                /// </summary>
                float GetResponse(const IDataPointCollection& data, unsigned int index) const;

            };

            /// <summary>   f(x,u,v) = I(x+u) - I(x+v) where x is the evaluated pixel in image I
            ///             and u and v are random 2-d pixel offsets within (sqrt(dimension)-1)/2 
            ///             of the pixel being evaluated. (equiv to (patch_size-1)/2)  </summary>
            class PixelSubtractionResponse
            {
            public:
                cv::Point offset_0;
                cv::Point offset_1;
                unsigned dimensions;

                PixelSubtractionResponse() {
                    dimensions = 0;
                    offset_0 = cv::Point(0,0);
                    offset_1 = cv::Point(0,0);
                }

                /// <summary>
                /// Creates PixelSubtractionResponse object, each 2-d offset is randomly generated
                /// </summary>
                PixelSubtractionResponse(Random& random,
                    unsigned int dimensions)
                    : dimensions(dimensions)
                {
                    // calculate upper and lower bounds
                    int ub = (int)ceil(sqrt(dimensions) / 2);
                    int lb = 0 - ub;

                    offset_0 = cv::Point(random.Next(lb,ub), random.Next(lb,ub));
                    offset_1 = cv::Point(random.Next(lb,ub), random.Next(lb,ub));
                }

                static PixelSubtractionResponse CreateRandom(Random& random, unsigned int dimensions);
                
                // IFeatureResponse implementation
                /// <summary>
                /// Calculates the difference of two pixels in a patch surrounding a pixel in an image.
                /// </summary>
                float GetResponse(const IDataPointCollection& data, unsigned int index) const;

            };
        }
    }
}
