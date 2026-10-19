#pragma once

// FaceSpike: copied from esp-dl v3.3.11 models/human_face_detect (MIT, see LICENSE_human_face_detect.txt).
// Arduino has no Kconfig for this component, so the menuconfig defaults are fixed here:
// MSR+MNP only, packed model linked as rodata (human_face_detect_model.cpp).
// DrowsyFW 0.2.4: added run_view() (FaceView below): rotate + resize in one sampling pass, no rotated frame.
#ifndef CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL
#define CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1 1
#define CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA 1
#define CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION 0
#define CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL 0
#endif

#include "dl_detect_base.hpp"
#include "dl_detect_espdet_postprocessor.hpp"
#include "dl_detect_mnp_postprocessor.hpp"
#include "dl_detect_msr_postprocessor.hpp"

// DrowsyFW: FaceView (face_view.h) = a rotated virtual image that is never built in memory.
// run_view() samples and converts raw pixels straight into each model input, so rotate + resize is one pass
// over the model input size only. Boxes and keypoints come back in virtual-image coordinates.
#include "face_view.h"

namespace human_face_detect {
// esp-dl's MSR/MNP postprocessors read the resize scale and crop offset from the preprocessor, which
// run_view() bypasses. These are the v3.3.11 parse_stage() bodies with the mapping passed in instead.
class ViewMSRPostprocessor : public dl::detect::AnchorBoxDetectPostprocessor {
public:
    using AnchorBoxDetectPostprocessor::AnchorBoxDetectPostprocessor;
    void set_map(float inv_sx, float inv_sy)
    {
        m_inv_sx = inv_sx;
        m_inv_sy = inv_sy;
    }
    void postprocess() override;

private:
    template <typename T>
    void parse_stage(dl::TensorBase *score, dl::TensorBase *box, const int stage_index);
    float m_inv_sx = 1, m_inv_sy = 1;
};

class ViewMNPPostprocessor : public dl::detect::AnchorBoxDetectPostprocessor {
public:
    using AnchorBoxDetectPostprocessor::AnchorBoxDetectPostprocessor;
    void set_map(float inv_sx, float inv_sy, int top_left_x, int top_left_y)
    {
        m_inv_sx = inv_sx;
        m_inv_sy = inv_sy;
        m_tlx = top_left_x;
        m_tly = top_left_y;
    }
    void postprocess() override;

private:
    template <typename T>
    void parse_stage(dl::TensorBase *score, dl::TensorBase *box, dl::TensorBase *landmark, const int stage_index);
    float m_inv_sx = 1, m_inv_sy = 1;
    int m_tlx = 0, m_tly = 0;
};

class MSR : public dl::detect::DetectImpl {
public:
    static constexpr float default_score_thr = 0.5;
    static constexpr float default_nms_thr = 0.5;
    MSR(const char *model_name, float score_thr, float nms_thr);
    ~MSR();
    dl::detect::Detect &set_score_thr(float score_thr, int idx = 0) override;
    dl::detect::Detect &set_nms_thr(float nms_thr, int idx = 0) override;
    std::list<dl::detect::result_t> &run_view(const dl::image::img_t &raw, const FaceView &view, float *pre_ms);
    // A/B check: number of model input bytes that differ between the stock resize of an already rotated
    // image and sampling from the raw image. -1 if the buffer could not be allocated.
    // ms[0] / ms[1] = time of the old preprocess (resize + colour) / the new one (sample + colour).
    int compare_input(const dl::image::img_t &rotated, const dl::image::img_t &raw, const FaceView &view, float ms[2]);

private:
    dl::image::ImagePreprocessor *m_view_preprocessor;
    ViewMSRPostprocessor *m_view_postprocessor;
    dl::image::NormQuant<int8_t, 3> *m_view_nq;   // colour LUT for writing the model input directly
};

class MNP {
private:
    dl::Model *m_model;
    dl::image::ImagePreprocessor *m_image_preprocessor;
    dl::detect::MNPPostprocessor *m_postprocessor;
    dl::image::ImagePreprocessor *m_view_preprocessor;
    ViewMNPPostprocessor *m_view_postprocessor;
    dl::image::NormQuant<int8_t, 3> *m_view_nq;

public:
    static constexpr float default_score_thr = 0.5;
    static constexpr float default_nms_thr = 0.5;
    MNP(const char *model_name, float score_thr, float nms_thr);
    ~MNP();
    MNP &set_score_thr(float score_thr);
    MNP &set_nms_thr(float nms_thr);
    dl::Model *get_raw_model();
    std::list<dl::detect::result_t> &run(const dl::image::img_t &img, std::list<dl::detect::result_t> &candidates);
    std::list<dl::detect::result_t> &run_view(const dl::image::img_t &raw,
                                              const FaceView &view,
                                              std::list<dl::detect::result_t> &candidates,
                                              float *pre_ms);
};

class MSRMNP : public dl::detect::Detect {
private:
    MSR m_msr;
    MNP m_mnp;

public:
    MSRMNP(const char *msr_model_name,
           float msr_score_thr,
           float msr_nms_thr,
           const char *mnp_model_name,
           float mnp_score_thr,
           float mnp_nms_thr) :
        m_msr(msr_model_name, msr_score_thr, msr_nms_thr), m_mnp(mnp_model_name, mnp_score_thr, mnp_nms_thr)
    {
    }

    std::list<dl::detect::result_t> &run(const dl::image::img_t &img) override;
    std::list<dl::detect::result_t> &run_view(const dl::image::img_t &raw, const FaceView &view, float *pre_ms);
    int compare_msr_input(const dl::image::img_t &rotated, const dl::image::img_t &raw, const FaceView &view, float ms[2])
    {
        return m_msr.compare_input(rotated, raw, view, ms);
    }
    Detect &set_score_thr(float score_thr, int idx) override;
    Detect &set_nms_thr(float nms_thr, int idx) override;
    dl::Model *get_raw_model(int idx) override;
};

class ESPDet : public dl::detect::DetectImpl {
public:
    static inline constexpr float default_score_thr = 0.5;
    static inline constexpr float default_nms_thr = 0.7;
    ESPDet(const char *model_name, float score_thr, float nms_thr);
};

} // namespace human_face_detect

class HumanFaceDetect : public dl::detect::DetectWrapper {
public:
    typedef enum {
        MSRMNP_S8_V1,
        ESPDET_PICO_224_224_FACE,
        ESPDET_PICO_416_416_FACE,
    } model_type_t;

    HumanFaceDetect(model_type_t model_type = static_cast<model_type_t>(CONFIG_DEFAULT_HUMAN_FACE_DETECT_MODEL),
                    bool lazy_load = true);

    // MSRMNP_S8_V1 only. pre_ms (optional) = time spent sampling + converting pixels for both stages.
    std::list<dl::detect::result_t> &run_view(const dl::image::img_t &raw, const FaceView &view, float *pre_ms = nullptr);
    int compare_msr_input(const dl::image::img_t &rotated, const dl::image::img_t &raw, const FaceView &view, float ms[2]);

private:
    void load_model() override;
    human_face_detect::MSRMNP *msrmnp();

    model_type_t m_model_type;
};
