/*
 * Copyright (C) 2008-12 Michal Perdoch
 * All rights reserved.
 *
 * This file is part of the HessianAffine detector and is made available under
 * the terms of the BSD license (see the COPYING file).
 *
 */

// Main File. Includes and uses the other files
//

#include <iostream>
#include <fstream>
#include <string>
#include <opencv2/core/core.hpp>

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "pyramid.h"
#include "helpers.h"
#include "affine.h"
#include "siftdesc.h"
#include "hesaff.h"

#define make_str(str_name, stream_input) \
    std::string str_name;\
{std::stringstream tmp_sstm;\
    tmp_sstm << stream_input;\
    str_name = tmp_sstm.str();\
};

#ifdef MYDEBUG
#undef MYDEBUG
#endif
//#define MYDEBUG

#ifdef MYDEBUG
#define print(msg) std::cout << msg << std::endl;
#define write(msg) std::cout << msg;
#else
#define print(msg);
#endif


#define ROTINVAR  // developing rotational invariance
#ifdef ROTINVAR
const int KPTS_DIM = 6;
#else
const int KPTS_DIM = 5;
#endif
const int DESC_DIM = 128;

typedef unsigned char uint8;

struct Keypoint
{
    float x, y, s;
    float a11,a12,a21,a22;
    #ifdef ROTINVAR
    float theta;
    #endif
    float response;
    int type;
    uint8 desc[DESC_DIM];
};


void rotate_downwards(float &a11, float &a12, float &a21, float &a22)
{
    //same as rectify_up_is_up but doest remove scale
    double a = a11, b = a12, c = a21, d = a22;
    double absdet_ = abs(a * d - b * c);
    double b2a2 = sqrt(b * b + a * a);
    //double sqtdet_ = sqrt(absdet_);
    //-
    a11 = b2a2;
    a12 = 0;
    a21 = (d * b + c * a) / (b2a2);
    a22 = absdet_ / b2a2;
}


void invE_to_invA(cv::Mat& invE, float &a11, float &a12, float &a21, float &a22)
{
    SVD svd_invE(invE, SVD::FULL_UV);
    float *diagE = (float *)svd_invE.w.data;
    diagE[0] = 1.0f / sqrt(diagE[0]);
    diagE[1] = 1.0f / sqrt(diagE[1]);
    // build new invA
    cv::Mat invA_ = svd_invE.u * cv::Mat::diag(svd_invE.w);
    a11 = invA_.at<float>(0,0);
    a12 = invA_.at<float>(0,1);
    a21 = invA_.at<float>(1,0);
    a22 = invA_.at<float>(1,1);
    // Rectify it (maintain scale)
    rotate_downwards(a11, a12, a21, a22);
}


cv::Mat invA_to_invE(float &a11, float &a12, float &a21, float &a22, float& s, float& desc_factor)
{
    float sc = desc_factor * s;
    cv::Mat invA = (cv::Mat_<float>(2,2) << a11, a12, a21, a22);

    //-----------------------
    // Convert invA to invE format
    SVD svd_invA(invA, SVD::FULL_UV);
    float *diagA = (float *)svd_invA.w.data;
    diagA[0] = 1.0f / (diagA[0] * diagA[0] * sc * sc);
    diagA[1] = 1.0f / (diagA[1] * diagA[1] * sc * sc);
    cv::Mat invE = svd_invA.u * cv::Mat::diag(svd_invA.w) * svd_invA.u.t();
    return invE;
}



struct AffineHessianDetector : public HessianDetector, AffineShape, HessianKeypointCallback, AffineShapeCallback
{
    public:
        // Member variables
        const cv::Mat image;
        SIFTDescriptor sift;
        std::vector<Keypoint> keys;

    public:
        // Constructor
        AffineHessianDetector(const cv::Mat &image,
                const PyramidParams &par,
                const AffineShapeParams &ap,
                const SIFTDescriptorParams &sp):
            HessianDetector(par), AffineShape(ap), image(image), sift(sp)
            {
                this->setHessianKeypointCallback(this); //Inherits from pyramid.h HessianDetector
                this->setAffineShapeCallback(this); // Inherits from affine.h AffineShape
            }

        int detect()
        {
            // Reset counters
            this->detectPyramidKeypoints(this->image);
            return this->keys.size();
        }

        void exportArrays(int nKpts, float *kpts, uint8 *desc)
        {
            // Exports keypoints and descriptors into preallocated numpy arrays
            for (size_t fx=0; fx < nKpts; fx++)
            {
                Keypoint &k = keys[fx];
                float x, y, a, b, c, d, s, det;
                float sc = AffineShape::par.mrSize * k.s;
                size_t rowk = fx * KPTS_DIM;
                size_t rowd = fx * DESC_DIM;
                // given kpts in invA format
                det = k.a11 * k.a22 - k.a12 * k.a21;
                x = k.x;
                y = k.y;
                // Incorporate the scale
                a = sc * k.a11 / (det);
                b = sc * k.a12 / (det);
                c = sc * k.a21 / (det);
                d = sc * k.a22 / (det);

                kpts[rowk + 0] = x;
                kpts[rowk + 1] = y;
                kpts[rowk + 2] = a;
                kpts[rowk + 3] = c;
                kpts[rowk + 4] = d;

                // Assign Descriptor Output
                for (size_t ix = 0; ix < DESC_DIM; ix++)
                {
                    desc[rowd + ix] = uint8(k.desc[ix]);
                }
            }
        }

        void write_features(char* img_fpath)
        {
            // Dump keypoints to disk in text format
            char suffix[] = ".hesaff.sift";
            int len = strlen(img_fpath)+strlen(suffix)+1;
            #ifdef WIN32
            char* out_fpath = new char[len];
            #else
            char out_fpath[len];
            #endif
            snprintf(out_fpath, len, "%s%s", img_fpath, suffix); out_fpath[len-1]=0;
            std::ofstream out(out_fpath);
            this->exportKeypoints(out);
            // Clean Up
            #ifdef WIN32
            delete[] out_fpath;
            #endif
        }

        void exportKeypoints(std::ostream &out)
        {
            /*Writes text keypoints in the invE format to a stdout stream
             * [iE_a, iE_b]
             * [iE_b, iE_d]
             */
            out << DESC_DIM << std::endl;
            int nKpts = keys.size();
            print("Writing " << nKpts << " keypoints");
            out << nKpts << std::endl;
            for (size_t i=0; i<nKpts; i++)
            {
                Keypoint &k = keys[i];
                float sc = AffineShape::par.mrSize * k.s;
                // Grav invA keypoints
                cv::Mat invA = (cv::Mat_<float>(2,2) << k.a11, k.a12, k.a21, k.a22);
                // Integrate the scale via signular value decomposition
                // Remember
                // A     = U *  S  * V.T   // SVD Step
                // invA  = V * 1/S * U.T   // Linear Algebra
                // E     = X *  W  * X.T
                // invE  = Y *  W  * X.T
                // E     = A.T  * A
                // invE  = invA * invA.T
                // X == Y, because E is symmetric
                // W == S^2
                // X == V

                // Decompose invA
                SVD svd_invA(invA, SVD::FULL_UV);
                float *diag_invA = (float *)svd_invA.w.data;
                // Integrate scale into 1/S and take squared inverst to make 1/W
                diag_invA[0] = 1.0f / (diag_invA[0]*diag_invA[0]*sc*sc);
                diag_invA[1] = 1.0f / (diag_invA[1]*diag_invA[1]*sc*sc);
                // Build the matrix invE
                // (I dont understand why U here, but it preserves the rotation I guess)
                // invE = (V * 1/S * U.T) * (U * 1/S * V.T)
                cv::Mat invE = svd_invA.u * cv::Mat::diag(svd_invA.w) * svd_invA.u.t();
                // Write inv(E) to out stream
                float e11 = invE.at<float>(0,0);
                float e12 = invE.at<float>(0,1); // also e12 because of E symetry
                float e22 = invE.at<float>(1,1);
#ifdef ROTINVAR
                float theta = k.theta;
                out << k.x << " " << k.y << " "
                    << e11 << " " << e12 << " "
                    << e22 << " " << theta;
#else
                out << k.x << " " << k.y << " "
                    << e11 << " " << e12 << " " << e22 ;
#endif
                for (size_t i=0; i < DESC_DIM; i++)
                    {
                    out << " " << int(k.desc[i]);
                    }
                out << std::endl;
            }
        }


        void onHessianKeypointDetected(const cv::Mat &blur, float x, float y,
                float s, float pixelDistance,
                int type, float response)
        {
            // A circular keypoint is detected. Adpat its shape to an ellipse
            findAffineShape(blur, x, y, s, pixelDistance, type, response);
        }

        void extractDesc(int nKpts, float* kpts, uint8* desc)
        {
            // Extract descriptors from user specified keypoints
            float x, y, ia, ib, ic, id;
            float sc;
            float a11, a12, a21, a22, s, theta;
            for(int fx=0; fx < (nKpts); fx++)
            {
                // 2D Array offsets
                size_t rowk = fx * KPTS_DIM;
                size_t rowd = fx * DESC_DIM;
                //Read a keypoint from the file
                x = kpts[rowk + 0];
                y = kpts[rowk + 1];
                // We are currently using inv(A) format in HotSpotter
                ia = kpts[rowk + 2];
                ib = 0;
                ic = kpts[rowk + 3];
                id = kpts[rowk + 4];
                #ifdef ROTINVAR
                theta = kpts[rowk + 5];
                #else
                theta = 0
                #endif
                // Extract scale.
                sc = sqrt(abs((ia * id) - (ib * ic)));
                // Deintegrate scale. Keep invA format
                s  = (sc / AffineShape::par.mrSize); // scale
                a11 = ia / sc;
                a12 = 0;
                a21 = ic / sc;
                a22 = id / sc;
                #ifdef MYDEBUG
                if (fx == 0)
                {
                    //print("[extractDesc.c]    sc = "  << sc);
                    //print("[extractDesc.c] iabcd = [" << ia << ", " << ib << ", " << ic << ", " << id << "] ");
                    //print("[extractDesc.c]    xy = (" <<  x << ", " <<  y << ") ");
                    //print("[extractDesc.c]    ab = [" << a11 << ", " << a12 << ",
                    //print("[extractDesc.c]    cd =  " << a21 << ", " << a22 << "] ");
                    //print("[extractDesc.c]     s = " << s);
                }
                #endif
                // now sample the patch (populates this->patch)
                if (!this->normalizeAffine(this->image, x, y, s, a11, a12, a21, a22, theta)) //affine.cpp
                {
                    this->populateDescriptor(desc, (fx * DESC_DIM)); // populate numpy array
                }
                else
                {
                    print("Failure!");
                }
            }
        }

        //------------------------------------------------------------
        // BEGIN void onAffineShapeFound
        // *
        // * Callback for when an affine shape is found.
        // * This is the stack traceback for this function:
        // * {detectPyramidKeypoints ->
        // *  detectOctaveKeypoints ->
        // *  localizeKeypoint ->
        // *  findAffineShape -> onAffineShapeFound}
        // * This function:
        // *   - Filters scales outside of bounds
        // *   - Computes the patch's rotation (if rotation_invariance is True)
        // *   - Computes the patch's SIFT Descriptor
        // *
        void onAffineShapeFound(const cv::Mat &blur, float x, float y,
                float s, float pixelDistance,
                float a11, float a12,
                float a21, float a22,
                int type, float response,
                int iters)
        {
            // type can be one of:
            //HESSIAN_DARK   = 0,
            //HESSIAN_BRIGHT = 1,
            //HESSIAN_SADDLE = 2,

            // check if detected keypoint is within scale thresholds
            float scale_min = AffineShape::par.scale_min;
            float scale_max = AffineShape::par.scale_max;
            float scale = AffineShape::par.mrSize * s;
            // negative thresholds turn the threshold test off
            if ((scale_min < 0 || scale >= scale_min) && (scale_max < 0 || scale <= scale_max))
            {
                //print("passed: " << scale)
                //print("scale_min: " << scale_min << "; scale_max: " << scale_max)
                //
                // Enforce the gravity vector: convert shape into a up is up frame
                rectifyAffineTransformationUpIsUp(a11, a12, a21, a22); // Helper
                float theta = 0;
                // now sample the patch (populates this->patch)
                if (!normalizeAffine(this->image, x, y, s, a11, a12, a21, a22, theta)) // affine.cpp
                {
                    // compute SIFT and append new keypoint and descriptor
                    //this->debug_patch();
                    this->keys.push_back(Keypoint());
                    Keypoint &k = this->keys.back();
                    k.type = type;
                    k.response = response;
                    k.x = x; k.y = y; k.s = s;
                    k.a11 = a11; k.a12 = a12; k.a21 = a21; k.a22 = a22;
                    this->populateDescriptor(k.desc, 0);
                }
            }
        }
        // END void onAffineShapeFound
        //------------------------------------------------------------
        void computeRotation(float x, float y, float s, float a11, float a12, float a21, float a22)
            {
            // TODO: Write code to extract the dominant gradient direction in C++
            //float rotation_invariance = AffineShape::par.rotation_invariance;
            }

        void populateDescriptor(uint8* desc, size_t offst)
        {
            this->sift.computeSiftDescriptor(this->patch);
            for (int ix=0; ix < DESC_DIM; ix++)
            {
                desc[offst + ix] = (uint8) sift.vec[ix];  // populate outvar
            }
        }

        void debug_patch()
            {
            //DBG: write out patches
            //make_str(fpath, "patches/patch_" << this->keys.size() << "c.png");
            //cv::imwrite(fpath, this->patch);
            }


};
// END class AffineHessianDetector


//----------------------------------------------
// BEGIN PYTHON BINDINGS
// * python's ctypes module can talk to extern c code
//http://nbviewer.ipython.org/github/pv/SciPy-CookBook/blob/master/ipython/Ctypes.ipynb
#ifdef __cplusplus
extern "C" {
#endif

// Python binds to extern C code
#define PYHESAFF extern HESAFF_EXPORT

    typedef void*(*allocer_t)(int, int*);

    PYHESAFF int detect(AffineHessianDetector* detector)
    {
    print("detector->detect");
    int nKpts = detector->detect();
    print("nKpts = " << nKpts);
    return nKpts;
    }

    // new hessian affine detector
    PYHESAFF AffineHessianDetector* new_hesaff_from_params(char* img_fpath,
            // Pyramid Params
            int   numberOfScales,
            float threshold,
            float edgeEigenValueRatio,
            int   border,
            // Affine Params Shape
            int   maxIterations,
            float convergenceThreshold,
            int   smmWindowSize,
            float mrSize,
            // SIFT params
            int spatialBins,
            int orientationBins,
            float maxBinValue,
            // Shared Pyramid + Affine
            float initialSigma,
            // Shared SIFT + Affine
            int patchSize,
            // My Params
            float scale_min,
            float scale_max,
            bool rotation_invariance)
                {
                print("making detector for " << img_fpath);
                print("make hesaff. img_fpath = " << img_fpath);
                // Read in image and convert to uint8
                cv::Mat tmp = cv::imread(img_fpath);
                cv::Mat image(tmp.rows, tmp.cols, CV_32FC1, Scalar(0));
                float *imgout = image.ptr<float>(0);
                uint8 *imgin  = tmp.ptr<uint8>(0);
                for (size_t i=tmp.rows*tmp.cols; i > 0; i--)
                    {
                    *imgout = (float(imgin[0]) + imgin[1] + imgin[2]) / 3.0f;
                    imgout++;
                    imgin+=3;
                    }

                // Define params
                SIFTDescriptorParams siftParams;
                PyramidParams pyrParams;
                AffineShapeParams affShapeParams;

                // Copy Pyramid params
                pyrParams.numberOfScales            = numberOfScales;
                pyrParams.threshold                 = threshold;
                pyrParams.edgeEigenValueRatio       = edgeEigenValueRatio;
                pyrParams.border                    = border;
                pyrParams.initialSigma              = initialSigma;

                // Copy Affine Shape params
                affShapeParams.maxIterations        = maxIterations;
                affShapeParams.convergenceThreshold = convergenceThreshold;
                affShapeParams.smmWindowSize        = smmWindowSize;
                affShapeParams.mrSize               = mrSize;
                affShapeParams.initialSigma         = initialSigma;
                affShapeParams.patchSize            = patchSize;

                // Copy SIFT params
                siftParams.spatialBins              = spatialBins;
                siftParams.orientationBins          = orientationBins;
                siftParams.maxBinValue              = maxBinValue;
                siftParams.patchSize                = patchSize;

                // Copy my params
                affShapeParams.scale_min            = scale_min;
                affShapeParams.scale_max            = scale_max;
                affShapeParams.rotation_invariance  = rotation_invariance;

#ifdef MYDEBUG
                //print("pyrParams.numberOfScales      = " << pyrParams.numberOfScales);
                //print("pyrParams.threshold           = " << pyrParams.threshold);
                //print("pyrParams.edgeEigenValueRatio = " << pyrParams.edgeEigenValueRatio);
                //print("pyrParams.border              = " << pyrParams.border);
                //print("pyrParams.initialSigma        = " << pyrParams.initialSigma);
                //print("affShapeParams.maxIterations        = " << affShapeParams.maxIterations);
                //print("affShapeParams.convergenceThreshold = " << affShapeParams.convergenceThreshold);
                //print("affShapeParams.smmWindowSize        = " << affShapeParams.smmWindowSize);
                //print("affShapeParams.mrSize               = " << affShapeParams.mrSize);
                //print("affShapeParams.initialSigma         = " << affShapeParams.initialSigma);
                //print("affShapeParams.patchSize            = " << affShapeParams.patchSize);
                //print("siftParams.spatialBins     = " << siftParams.spatialBins);
                //print("siftParams.orientationBins = " << siftParams.orientationBins);
                //print("siftParams.maxBinValue     = " << siftParams.maxBinValue);
                //print("siftParams.patchSize       = " << siftParams.patchSize);
                print("affShapeParams.scale_min            = " << scale_min);
                print("affShapeParams.scale_max            = " << scale_max);
                print("affShapeParams.rotation_invariance  = " << rotation_invariance);
#endif

                // Create detector
                AffineHessianDetector* detector = new AffineHessianDetector(image, pyrParams, affShapeParams, siftParams);
                return detector;
                }

    // new hessian affine detector WRAPPER
    PYHESAFF AffineHessianDetector* new_hesaff(char* img_fpath)
        {
        // Pyramid Params
        int   numberOfScales = 3;
        float threshold = 16.0f / 3.0f;
        float edgeEigenValueRatio = 10.0f;
        int   border = 5;
        // Affine Params Shape
        int   maxIterations = 16;
        float convergenceThreshold = 0.05;
        int   smmWindowSize = 19;
        float mrSize = 3.0f * sqrt(3.0f);
        // SIFT params
        int spatialBins = 4;
        int orientationBins = 8;
        float maxBinValue = 0.2f;
        // Shared Pyramid + Affine
        float initialSigma = 1.6f;
        // Shared SIFT + Affine
        int patchSize = 41;
        // My params
        float scale_min = -1;
        float scale_max = -1;
        bool rotation_invariance = false;

        AffineHessianDetector* detector = new_hesaff_from_params(img_fpath,
                numberOfScales, threshold, edgeEigenValueRatio, border,
                maxIterations, convergenceThreshold, smmWindowSize, mrSize,
                spatialBins, orientationBins, maxBinValue, initialSigma, patchSize,
                scale_min, scale_max, rotation_invariance);
        return detector;
        }

    // extract descriptors from user specified keypoints
    PYHESAFF void extractDesc(AffineHessianDetector* detector,
            int nKpts, float* kpts, uint8* desc)
        {
        print("detector->extractDesc");
        detector->extractDesc(nKpts, kpts, desc);
        print("extracted nKpts = " << nKpts);
        }

    // export current detections to numpy arrays
    PYHESAFF void exportArrays(AffineHessianDetector* detector,
            int nKpts, float *kpts, uint8 *desc)
        {
        print("detector->exportArrays(" << nKpts << ")");
        //print("detector->exportArrays kpts[0]" << kpts[0] << ")");
        //print("detector->exportArrays desc[0]" << (int) desc[0] << ")");
        detector->exportArrays(nKpts, kpts, desc);
        //print("detector->exportArrays kpts[0]" << kpts[0] << ")");
        //print("detector->exportArrays desc[0]" << (int) desc[0] << ")");
        print("FINISHED detector->exportArrays");
        }

    // dump current detections to disk
    PYHESAFF void writeFeatures(AffineHessianDetector* detector,
            char* img_fpath)
        {
        print("detector->write_features");
        detector->write_features(img_fpath);
        }

#ifdef __cplusplus
}
#endif
// END PYTHON BINDINGS
//----------------------------------------------


//-------------------------------
// int main
// * program entry point for command line use if we build the executable
int main(int argc, char **argv)
{
    if (argc>1)
        {
        print("[hesaff.c] main()");
        char* img_fpath = argv[1];
        int nKpts;
        AffineHessianDetector* detector = new_hesaff(img_fpath);
        nKpts = detect(detector);
        writeFeatures(detector, img_fpath);
        }
    else
        {
        printf("\nUsage: ell_desc image_name.png kpts_file.txt\nDescribes elliptical keypoints (with gravity vector) given in kpts_file.txt using a SIFT descriptor.\n\n");
        }
}
