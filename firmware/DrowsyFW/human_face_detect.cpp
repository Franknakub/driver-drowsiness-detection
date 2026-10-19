#include "human_face_detect.hpp"
#include <algorithm>
#include <filesystem>
#include "dl_math.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#if CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_RODATA
extern const uint8_t human_face_detect_espdl[]; // FaceSpike: defined in human_face_detect_model.cpp
static const char *path = (const char *)human_face_detect_espdl;
#elif CONFIG_HUMAN_FACE_DETECT_MODEL_IN_FLASH_PARTITION
static const char *path = "human_face_det";
#else
#if !defined(CONFIG_BSP_SD_MOUNT_POINT)
#define CONFIG_BSP_SD_MOUNT_POINT "/sdcard"
#endif
#endif
namespace human_face_detect {
static dl::image::NormQuant<int8_t, 3> *make_view_nq(dl::TensorBase *in);

MSR::MSR(const char *model_name, float score_thr, float nms_thr)
{
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    auto sd_path =
        std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_HUMAN_FACE_DETECT_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#endif
    m_model->minimize();
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, true);
    m_postprocessor =
        new dl::detect::MSRPostprocessor(m_model,
                                         m_image_preprocessor,
                                         score_thr,
                                         nms_thr,
                                         10,
                                         {{8, 8, 9, 9, {{16, 16}, {32, 32}}}, {16, 16, 9, 9, {{64, 64}, {128, 128}}}});
    m_view_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, true);
    m_view_nq = make_view_nq(m_view_preprocessor->get_model_input());
    m_view_postprocessor =
        new ViewMSRPostprocessor(m_model,
                                 m_view_preprocessor,
                                 score_thr,
                                 nms_thr,
                                 10,
                                 {{8, 8, 9, 9, {{16, 16}, {32, 32}}}, {16, 16, 9, 9, {{64, 64}, {128, 128}}}});
}

MSR::~MSR()
{
    delete m_view_nq;
    delete m_view_preprocessor;
    delete m_view_postprocessor;
}

dl::detect::Detect &MSR::set_score_thr(float score_thr, int idx)
{
    m_view_postprocessor->set_score_thr(score_thr);
    return DetectImpl::set_score_thr(score_thr, idx);
}

dl::detect::Detect &MSR::set_nms_thr(float nms_thr, int idx)
{
    m_view_postprocessor->set_nms_thr(nms_thr);
    return DetectImpl::set_nms_thr(nms_thr, idx);
}

MNP::MNP(const char *model_name, float score_thr, float nms_thr)
{
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    auto sd_path =
        std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_HUMAN_FACE_DETECT_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#endif
    m_model->minimize();
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, true);
    m_postprocessor = new dl::detect::MNPPostprocessor(
        m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{1, 1, 0, 0, {{48, 48}}}});
    m_view_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {1, 1, 1}, true);
    m_view_nq = make_view_nq(m_view_preprocessor->get_model_input());
    m_view_postprocessor = new ViewMNPPostprocessor(
        m_model, m_view_preprocessor, score_thr, nms_thr, 10, {{1, 1, 0, 0, {{48, 48}}}});
}

MNP::~MNP()
{
    delete m_model;
    delete m_image_preprocessor;
    delete m_postprocessor;
    delete m_view_preprocessor;
    delete m_view_postprocessor;
    delete m_view_nq;
}

MNP &MNP::set_score_thr(float score_thr)
{
    m_postprocessor->set_score_thr(score_thr);
    m_view_postprocessor->set_score_thr(score_thr);
    return *this;
}

MNP &MNP::set_nms_thr(float nms_thr)
{
    m_postprocessor->set_nms_thr(nms_thr);
    m_view_postprocessor->set_nms_thr(nms_thr);
    return *this;
}

dl::Model *MNP::get_raw_model()
{
    return m_model;
}

std::list<dl::detect::result_t> &MNP::run(const dl::image::img_t &img, std::list<dl::detect::result_t> &candidates)
{
    DL_LOG_INFER_LATENCY_ARRAY_INIT_WITH_SIZE(3, 10);
    m_postprocessor->clear_result();
    for (auto &candidate : candidates) {
        int center_x = (candidate.box[0] + candidate.box[2]) >> 1;
        int center_y = (candidate.box[1] + candidate.box[3]) >> 1;
        int side = DL_MAX(candidate.box[2] - candidate.box[0], candidate.box[3] - candidate.box[1]);
        candidate.box[0] = center_x - (side >> 1);
        candidate.box[1] = center_y - (side >> 1);
        candidate.box[2] = candidate.box[0] + side;
        candidate.box[3] = candidate.box[1] + side;
        candidate.limit_box(img.width, img.height);

        DL_LOG_INFER_LATENCY_ARRAY_START(0);
        m_image_preprocessor->preprocess(img, candidate.box);
        DL_LOG_INFER_LATENCY_ARRAY_END(0);

        DL_LOG_INFER_LATENCY_ARRAY_START(1);
        m_model->run();
        DL_LOG_INFER_LATENCY_ARRAY_END(1);

        DL_LOG_INFER_LATENCY_ARRAY_START(2);
        m_postprocessor->postprocess();
        DL_LOG_INFER_LATENCY_ARRAY_END(2);
    }
    m_postprocessor->nms();
    std::list<dl::detect::result_t> &result = m_postprocessor->get_result(img.width, img.height);
    if (candidates.size() > 0) {
        DL_LOG_INFER_LATENCY_ARRAY_PRINT(0, "detect", "pre");
        DL_LOG_INFER_LATENCY_ARRAY_PRINT(1, "detect", "model");
        DL_LOG_INFER_LATENCY_ARRAY_PRINT(2, "detect", "post");
    }
    return result;
}

std::list<dl::detect::result_t> &MSRMNP::run(const dl::image::img_t &img)
{
    std::list<dl::detect::result_t> &candidates = m_msr.run(img);
    return m_mnp.run(img, candidates);
}

std::list<dl::detect::result_t> &MSRMNP::run_view(const dl::image::img_t &raw, const FaceView &view, float *pre_ms)
{
    std::list<dl::detect::result_t> &candidates = m_msr.run_view(raw, view, pre_ms);
    return m_mnp.run_view(raw, view, candidates, pre_ms);
}

dl::detect::Detect &MSRMNP::set_score_thr(float score_thr, int idx)
{
    assert(idx == 0 || idx == 1);
    if (idx == 0) {
        m_msr.set_score_thr(score_thr);
    } else {
        m_mnp.set_score_thr(score_thr);
    }
    return *this;
}

dl::detect::Detect &MSRMNP::set_nms_thr(float nms_thr, int idx)
{
    assert(idx == 0 || idx == 1);
    if (idx == 0) {
        m_msr.set_nms_thr(nms_thr);
    } else {
        m_mnp.set_nms_thr(nms_thr);
    }
    return *this;
}

dl::Model *MSRMNP::get_raw_model(int idx)
{
    assert(idx == 0 || idx == 1);
    if (idx == 0) {
        return m_msr.get_raw_model();
    } else {
        return m_mnp.get_raw_model();
    }
}

ESPDet::ESPDet(const char *model_name, float score_thr, float nms_thr)
{
#if !CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
    m_model = new dl::Model(
        path, model_name, static_cast<fbs::model_location_type_t>(CONFIG_HUMAN_FACE_DETECT_MODEL_LOCATION));
#else
    auto sd_path =
        std::filesystem::path(CONFIG_BSP_SD_MOUNT_POINT) / CONFIG_HUMAN_FACE_DETECT_MODEL_SDCARD_DIR / model_name;
    m_model = new dl::Model(sd_path.c_str(), fbs::MODEL_LOCATION_IN_SDCARD);
#endif
    m_model->minimize();
    m_image_preprocessor = new dl::image::ImagePreprocessor(m_model, {0, 0, 0}, {255, 255, 255});
    m_image_preprocessor->enable_letterbox({114, 114, 114});
    m_postprocessor = new dl::detect::ESPDetPostProcessor(
        m_model, m_image_preprocessor, score_thr, nms_thr, 10, {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
}

// ---- DrowsyFW: run on a FaceView (rotate + resize + colour in one pass straight into the model input) ----
// esp-dl 3.3.11 ImagePreprocessor::preprocess(img, M) cannot be used: its gen_xy_map() allocates the warp maps
// m_y1/m_x2 with the wrong length (dst_height vs dst_width) and corrupts the heap whenever the model input is
// not square (MSR). Instead each model input pixel is picked here exactly as the old path did it (esp-dl's
// nearest resize of the rotated frame: truncate, clamp to the crop), mapped through FaceView (the same fixed
// point as the old full-frame rotate), and converted with esp-dl's own scalar RGB565 -> BGR888 NormQuant code
// (same LUT the preprocessor builds). Result: the model input is byte-identical to rotating the whole frame,
// without touching the rest of the frame. PSRAM reads are the cost, not arithmetic.

// Same normalisation as ImagePreprocessor(model, {0,0,0}, {1,1,1}, rgb_swap = true) on an int8 input
static dl::image::NormQuant<int8_t, 3> *make_view_nq(dl::TensorBase *in)
{
    assert(in->dtype == dl::DATA_TYPE_INT8 && in->shape[3] == 3);
    return new dl::image::NormQuant<int8_t, 3>({0, 0, 0}, {1, 1, 1}, in->exponent);
}

// Model input = virtual-image area [x0, x1) x [y0, y1) resized like esp-dl's resize_nn:
// u = min(int(j * (x1 - x0) / dw) + x0, x1 - 1). Virtual pixels outside the raw image = black (0).
static constexpr int kMaxView = 256;
static void view_preprocess(dl::image::ImagePreprocessor *pre,
                            const dl::image::NormQuant<int8_t, 3> *nq,
                            const dl::image::img_t &raw,
                            const FaceView &v,
                            int x0, int y0, int x1, int y1)
{
    // per column: ang = 0 -> idx_a * u · ang != 0 -> u * dpx / u * dpy (the same terms FaceView::raw_index adds)
    static int ca[kMaxView], cb[kMaxView];
    dl::TensorBase *in = pre->get_model_input();
    const int dw = in->shape[2], dh = in->shape[1];
    assert(dw <= kMaxView && raw.pix_type == dl::image::DL_IMAGE_PIX_TYPE_RGB565BE);
    const float isx = static_cast<float>(x1 - x0) / dw, isy = static_cast<float>(y1 - y0) / dh;
    for (int j = 0; j < dw; j++) {
        const int u = std::min(static_cast<int>(j * isx) + x0, x1 - 1);
        ca[j] = v.ang ? u * v.dpx : v.idx_a * u;
        cb[j] = v.ang ? u * v.dpy : 0;
    }
    const dl::image::RGB5652RGB888<true, true, dl::image::NormQuant<int8_t, 3>> cvt(*nq);
    const uint16_t *src = (const uint16_t *)raw.data;
    uint8_t *dst = (uint8_t *)in->data;
    for (int i = 0; i < dh; i++) {
        const int y = std::min(static_cast<int>(i * isy) + y0, y1 - 1);
        if (!v.ang) {
            const uint16_t *row = src + v.idx_c + v.idx_b * y;
            for (int j = 0; j < dw; j++, dst += 3) cvt((const uint8_t *)&row[ca[j]], dst);
            continue;
        }
        int32_t fx0, fy0;
        v.row_start(y, fx0, fy0);
        for (int j = 0; j < dw; j++, dst += 3) {
            const int px = (fx0 + ca[j]) >> 16, py = (fy0 + cb[j]) >> 16;
            const uint16_t p = ((unsigned)px < (unsigned)v.width && (unsigned)py < (unsigned)v.height)
                ? src[v.idx_c + v.idx_a * px + v.idx_b * py]
                : 0;
            cvt((const uint8_t *)&p, dst);
        }
    }
}

std::list<dl::detect::result_t> &MSR::run_view(const dl::image::img_t &raw, const FaceView &view, float *pre_ms)
{
    dl::TensorBase *in = m_view_preprocessor->get_model_input();
    float sx = (float)view.width / in->shape[2], sy = (float)view.height / in->shape[1];
    int64_t t = esp_timer_get_time();
    view_preprocess(m_view_preprocessor, m_view_nq, raw, view, 0, 0, view.width, view.height);
    if (pre_ms) *pre_ms = (esp_timer_get_time() - t) / 1000.0f;
    m_model->run();
    m_view_postprocessor->clear_result();
    m_view_postprocessor->set_map(sx, sy);
    m_view_postprocessor->postprocess();
    return m_view_postprocessor->get_result(view.width, view.height);
}

int MSR::compare_input(const dl::image::img_t &rotated, const dl::image::img_t &raw, const FaceView &view, float ms[2])
{
    dl::TensorBase *in = m_view_preprocessor->get_model_input();
    int n = in->get_bytes();
    uint8_t *ref = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!ref) return -1;
    int64_t t0 = esp_timer_get_time();
    m_image_preprocessor->preprocess(rotated);
    int64_t t1 = esp_timer_get_time();
    memcpy(ref, in->data, n);
    int64_t t2 = esp_timer_get_time();
    view_preprocess(m_view_preprocessor, m_view_nq, raw, view, 0, 0, view.width, view.height);
    ms[0] = (t1 - t0) / 1000.0f;
    ms[1] = (esp_timer_get_time() - t2) / 1000.0f;
    const uint8_t *cur = (const uint8_t *)in->data;
    int diff = 0;
    for (int i = 0; i < n; i++) diff += ref[i] != cur[i];
    heap_caps_free(ref);
    return diff;
}

std::list<dl::detect::result_t> &MNP::run_view(const dl::image::img_t &raw,
                                               const FaceView &view,
                                               std::list<dl::detect::result_t> &candidates,
                                               float *pre_ms)
{
    dl::TensorBase *in = m_view_preprocessor->get_model_input();
    const int iw = in->shape[2], ih = in->shape[1];
    int64_t pre_us = 0;
    m_view_postprocessor->clear_result();
    for (auto &candidate : candidates) {
        // same square crop as MNP::run, in virtual-image coordinates
        int center_x = (candidate.box[0] + candidate.box[2]) >> 1;
        int center_y = (candidate.box[1] + candidate.box[3]) >> 1;
        int side = DL_MAX(candidate.box[2] - candidate.box[0], candidate.box[3] - candidate.box[1]);
        candidate.box[0] = center_x - (side >> 1);
        candidate.box[1] = center_y - (side >> 1);
        candidate.box[2] = candidate.box[0] + side;
        candidate.box[3] = candidate.box[1] + side;
        candidate.limit_box(view.width, view.height);
        int cw = candidate.box[2] - candidate.box[0], ch = candidate.box[3] - candidate.box[1];
        if (cw <= 0 || ch <= 0) continue;   // MNP::run would assert on this empty crop area
        float sx = (float)cw / iw, sy = (float)ch / ih;
        int64_t t = esp_timer_get_time();
        view_preprocess(m_view_preprocessor, m_view_nq, raw, view,
                        candidate.box[0], candidate.box[1], candidate.box[2], candidate.box[3]);
        pre_us += esp_timer_get_time() - t;
        m_model->run();
        m_view_postprocessor->set_map(sx, sy, candidate.box[0], candidate.box[1]);
        m_view_postprocessor->postprocess();
    }
    if (pre_ms) *pre_ms += pre_us / 1000.0f;
    m_view_postprocessor->nms();
    return m_view_postprocessor->get_result(view.width, view.height);
}

// parse_stage() bodies below: esp-dl v3.3.11 vision/detect/dl_detect_{msr,mnp}_postprocessor.cpp (MIT),
// with the preprocessor's resize scale / crop offset replaced by the set_map() values.
template <typename T>
void ViewMSRPostprocessor::parse_stage(dl::TensorBase *score, dl::TensorBase *box, const int stage_index)
{
    int stride_y = m_stages[stage_index].stride_y;
    int stride_x = m_stages[stage_index].stride_x;
    int offset_y = m_stages[stage_index].offset_y;
    int offset_x = m_stages[stage_index].offset_x;
    std::vector<std::vector<int>> &anchor_shape = m_stages[stage_index].anchor_shape;

    int H = score->shape[1];
    int W = score->shape[2];
    int A = anchor_shape.size();
    int C = score->shape[3] / A;
    T *score_ptr = (T *)score->data;
    T *box_ptr = (T *)box->data;
    float score_exp = DL_SCALE(score->exponent);
    float box_exp = DL_SCALE(box->exponent);
    T score_thr_quant = dl::quantize<T>(dl::math::inverse_sigmoid(m_score_thr), 1.f / score_exp);
    float inv_resize_scale_x = m_inv_sx;
    float inv_resize_scale_y = m_inv_sy;

    for (size_t y = 0; y < H; y++) {
        for (size_t x = 0; x < W; x++) {
            for (size_t a = 0; a < A; a++) {
                for (size_t c = 0; c < C; c++) {
                    if (*score_ptr > score_thr_quant) {
                        int center_y = y * stride_y + offset_y;
                        int center_x = x * stride_x + offset_x;
                        int anchor_h = anchor_shape[a][0];
                        int anchor_w = anchor_shape[a][1];
                        dl::detect::result_t new_box = {
                            (int)c,
                            dl::math::sigmoid(dl::dequantize(*score_ptr, score_exp)),
                            {(int)((center_x - (anchor_w >> 1) + anchor_w * dl::dequantize(box_ptr[0], box_exp)) *
                                   inv_resize_scale_x),
                             (int)((center_y - (anchor_h >> 1) + anchor_h * dl::dequantize(box_ptr[1], box_exp)) *
                                   inv_resize_scale_y),
                             (int)((center_x + anchor_w - (anchor_w >> 1) +
                                    anchor_w * dl::dequantize(box_ptr[2], box_exp)) *
                                   inv_resize_scale_x),
                             (int)((center_y + anchor_h - (anchor_h >> 1) +
                                    anchor_h * dl::dequantize(box_ptr[3], box_exp)) *
                                   inv_resize_scale_y)},
                            {}};
                        m_box_list.insert(
                            std::upper_bound(m_box_list.begin(), m_box_list.end(), new_box, dl::detect::greater_box),
                            new_box);
                    }
                    score_ptr++;
                    box_ptr += 4;
                }
            }
        }
    }
}

void ViewMSRPostprocessor::postprocess()
{
    dl::TensorBase *score0 = m_model->get_output("score0");
    dl::TensorBase *bbox0 = m_model->get_output("box0");
    dl::TensorBase *score1 = m_model->get_output("score1");
    dl::TensorBase *bbox1 = m_model->get_output("box1");
    if (score0->dtype == dl::DATA_TYPE_INT8) {
        parse_stage<int8_t>(score0, bbox0, 0);
        parse_stage<int8_t>(score1, bbox1, 1);
    } else {
        parse_stage<int16_t>(score0, bbox0, 0);
        parse_stage<int16_t>(score1, bbox1, 1);
    }
    nms();
}

template <typename T>
void ViewMNPPostprocessor::parse_stage(dl::TensorBase *score,
                                       dl::TensorBase *box,
                                       dl::TensorBase *landmark,
                                       const int stage_index)
{
    std::vector<std::vector<int>> &anchor_shape = m_stages[stage_index].anchor_shape;

    int H = score->shape[1];
    int W = score->shape[2];
    int A = anchor_shape.size();
    int C = score->shape[3] / A;
    T *score_ptr = (T *)score->data;
    T *box_ptr = (T *)box->data;
    T *landmark_ptr = (T *)landmark->data;
    float score_exp = DL_SCALE(score->exponent);
    float box_exp = DL_SCALE(box->exponent);
    float landmark_exp = DL_SCALE(landmark->exponent);
    float isx = m_inv_sx, isy = m_inv_sy;
    int tlx = m_tlx, tly = m_tly;

    for (size_t y = 0; y < H; y++) {
        for (size_t x = 0; x < W; x++) {
            for (size_t a = 0; a < A; a++) {
                float scores[C];
                scores[0] = dl::dequantize(score_ptr[0], score_exp);
                float max_score = scores[0];
                for (int i = 1; i < C; i++) {
                    scores[i] = dl::dequantize(score_ptr[i], score_exp);
                    if (max_score < scores[i]) max_score = scores[i];
                }
                float sum = 0;
                for (int i = 0; i < C; i++) sum += expf(scores[i] - max_score);
                max_score = 1. / sum;

                if (max_score > m_score_thr) {
                    int anchor_h = anchor_shape[a][0];
                    int anchor_w = anchor_shape[a][1];
                    auto lx = [&](int k) {
                        return (int)(anchor_w * dl::dequantize(landmark_ptr[k], landmark_exp) * isx + tlx);
                    };
                    auto ly = [&](int k) {
                        return (int)(anchor_h * dl::dequantize(landmark_ptr[k], landmark_exp) * isy + tly);
                    };
                    dl::detect::result_t new_box = {
                        0,
                        max_score,
                        {(int)(anchor_w * dl::dequantize(box_ptr[0], box_exp) * isx + tlx),
                         (int)(anchor_h * dl::dequantize(box_ptr[1], box_exp) * isy + tly),
                         (int)((anchor_w * dl::dequantize(box_ptr[2], box_exp) + anchor_w) * isx + tlx),
                         (int)((anchor_h * dl::dequantize(box_ptr[3], box_exp) + anchor_h) * isy + tly)},
                        {lx(0), ly(1), lx(2), ly(3), lx(4), ly(5), lx(6), ly(7), lx(8), ly(9)}};
                    m_box_list.insert(
                        std::upper_bound(m_box_list.begin(), m_box_list.end(), new_box, dl::detect::greater_box),
                        new_box);
                }
                score_ptr += C;
                box_ptr += 4;
                landmark_ptr += 10;
            }
        }
    }
}

void ViewMNPPostprocessor::postprocess()
{
    dl::TensorBase *score = m_model->get_output("score");
    dl::TensorBase *bbox = m_model->get_output("box");
    dl::TensorBase *landmark = m_model->get_output("landmark");
    if (score->dtype == dl::DATA_TYPE_INT8) {
        parse_stage<int8_t>(score, bbox, landmark, 0);
    } else {
        parse_stage<int16_t>(score, bbox, landmark, 0);
    }
}

} // namespace human_face_detect

human_face_detect::MSRMNP *HumanFaceDetect::msrmnp()
{
    if (!m_model) load_model();
    assert(m_model_type == MSRMNP_S8_V1);
    return static_cast<human_face_detect::MSRMNP *>(m_model);
}

std::list<dl::detect::result_t> &HumanFaceDetect::run_view(const dl::image::img_t &raw, const FaceView &view, float *pre_ms)
{
    return msrmnp()->run_view(raw, view, pre_ms);
}

int HumanFaceDetect::compare_msr_input(const dl::image::img_t &rotated, const dl::image::img_t &raw, const FaceView &view, float ms[2])
{
    return msrmnp()->compare_msr_input(rotated, raw, view, ms);
}

HumanFaceDetect::HumanFaceDetect(model_type_t model_type, bool lazy_load) : m_model_type(model_type)
{
    switch (model_type) {
    case model_type_t::MSRMNP_S8_V1:
        m_score_thr[0] = human_face_detect::MSR::default_score_thr;
        m_nms_thr[0] = human_face_detect::MSR::default_nms_thr;
        m_score_thr[1] = human_face_detect::MNP::default_score_thr;
        m_nms_thr[1] = human_face_detect::MNP::default_nms_thr;
        break;
    case model_type_t::ESPDET_PICO_224_224_FACE:
    case model_type_t::ESPDET_PICO_416_416_FACE:
        m_score_thr[0] = human_face_detect::ESPDet::default_score_thr;
        m_nms_thr[0] = human_face_detect::ESPDet::default_nms_thr;
        break;
    }
    if (lazy_load) {
        m_model = nullptr;
    } else {
        load_model();
    }
}

void HumanFaceDetect::load_model()
{
    switch (m_model_type) {
    case model_type_t::MSRMNP_S8_V1: {
#if CONFIG_FLASH_HUMAN_FACE_DETECT_MSRMNP_S8_V1 || CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
        m_model = new human_face_detect::MSRMNP("human_face_detect_msr_s8_v1.espdl",
                                                m_score_thr[0],
                                                m_nms_thr[0],
                                                "human_face_detect_mnp_s8_v1.espdl",
                                                m_score_thr[1],
                                                m_nms_thr[1]);
#else
        ESP_LOGE("human_face_detect", "human_face_detect_msrmnp_s8_v1 is not selected in menuconfig.");
#endif
        break;
    }
    case model_type_t::ESPDET_PICO_224_224_FACE: {
#if CONFIG_FLASH_ESPDET_PICO_224_224_FACE || CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
        m_model = new human_face_detect::ESPDet("espdet_pico_224_224_face.espdl", m_score_thr[0], m_nms_thr[0]);
#else
        ESP_LOGE("human_face_detect", "espdet_pico_224_224_face is not selected in menuconfig.");
#endif
        break;
    }
    case model_type_t::ESPDET_PICO_416_416_FACE: {
#if CONFIG_FLASH_ESPDET_PICO_416_416_FACE || CONFIG_HUMAN_FACE_DETECT_MODEL_IN_SDCARD
        m_model = new human_face_detect::ESPDet("espdet_pico_416_416_face.espdl", m_score_thr[0], m_nms_thr[0]);
#else
        ESP_LOGE("human_face_detect", "espdet_pico_416_416_face is not selected in menuconfig.");
#endif
        break;
    }
    }
}
