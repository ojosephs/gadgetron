//
// Created by dchansen on 3/9/20.
//
#include "demons_registration.h"
#include <unordered_set>

#include "PureGadget.h"
#include "cmr_parametric_mapping.h"
#include "hoNDArray_fileio.h"
#include "hoNDArray_math.h"
#include "hoNDArray_utils.h"
#include "mri_core_data.h"
#include "mri_core_def.h"
#include "t1fit.h"

#include <range/v3/view.hpp>
#include <algorithm>

namespace Gadgetron {

    class T1MocoGadget : public Core::ChannelGadget<IsmrmrdImageArray> {

    public:
        T1MocoGadget(const Core::Context& context, const Core::GadgetProperties& properties)
            : Core::ChannelGadget<IsmrmrdImageArray>(context, properties)
            , TIs{ *(context.header.sequenceParameters->TI) }
            , field_strength{ *context.header.acquisitionSystemInformation->systemFieldStrength_T } { }


        NODE_PROPERTY(correction_factor, float, "Empirical correction factor for T1", 1.0365f);
        NODE_PROPERTY(regularization_sigma,float,"Gaussian regularizatin for registration",2.0f);
        NODE_PROPERTY(demons_iterations,unsigned int ,"Number of iterations for the demons registration",40);
        NODE_PROPERTY(step_size,float,"Maximum step size for demons registration (between 0.1 and 2.0)",2.0f);
        NODE_PROPERTY(iterations, unsigned int, "Number of iterations of demons registration and T1 fit",5);
        NODE_PROPERTY(scales, unsigned int, "Number of image scales to use",1);

    private:
        void process(Core::InputChannel<IsmrmrdImageArray>& input, Core::OutputChannel& out) final override {

            GDEBUG("Sigma %f demons_iterations %i Step size %f iterations %i\n",regularization_sigma,demons_iterations,step_size, iterations);

            for (auto images : input) {

                auto TI_values = extract_MOLLI_TI(*images.acq_headers_);

                auto group_data = extract_groups(*images.acq_headers_, images.data_, TI_values);

//                auto [vector_field, resorted_images] = group_t1_registration(group_data);


                auto data_dims = images.data_.dimensions();
                images.data_.reshape( data_dims[0], data_dims[1], -1 );

                auto vector_fields = group_t1_registration(group_data);
//                auto vector_field = T1::multi_scale_t1_registration(images.data_, TI_values,scales,iterations,{demons_iterations,regularization_sigma,step_size});

                auto vector_field = T1::t1_moco_cmr(images.data_,TI_values,iterations);
                auto moco_images = T1::deform_groups(images.data_, vector_field);

                auto phase_corrected = T1::phase_correct(moco_images, TI_values);

                auto [A, B, T1star] = T1::fit_T1_3param(phase_corrected, TI_values);

                B /= A;
                B -= 1;

                auto T1 = T1star;
                T1 *= B;
                T1 *= correction_factor;

                clean_image(T1);

                perform_hole_filling(T1);
                auto header               = images.headers_[0];
                header.data_type          = ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE;
                header.image_series_index = 4;
                auto meta                 = create_T1_meta(images.meta_.front());

                // send original images
                images.data_.reshape(data_dims);
                set_RAW_headers_and_meta(images,TI_values);
                out.push(images);

                // send MOCO images
                images.data_ = hoNDArray<std::complex<float>>(phase_corrected);
                images.data_.reshape(data_dims);


                auto mag_images = images;
                set_MOCO_MAG_headers_and_meta(mag_images,TI_values);
                out.push(std::move(mag_images));

                set_PSIR_headers_and_meta(images, TI_values);
                out.push(std::move(images));

                // send out T1 map                
                out.push(Core::Image<float>{ header, std::move(T1), meta });
            }
        }

        ISMRMRD::MetaContainer create_T1_meta(ISMRMRD::MetaContainer meta) const {

            double scaling_factor = 1;
            double window_center  = 1300;
            std::string lut
                = std::abs(field_strength - float(1.5)) < 1e-1 ? "GadgetronT1_IR_1_5T.pal" : "GadgetronT1_IR_3T.pal";

            std::ostringstream ostr;
            ostr << "x" << scaling_factor;
            std::string scalingStr = ostr.str();

            std::ostringstream ostr_unit;
            ostr_unit << std::setprecision(3) << 1.0f / scaling_factor << "ms";
            std::string unitStr = ostr_unit.str();

            meta.set(GADGETRON_DATA_ROLE, GADGETRON_IMAGE_T1MAP);
            meta.append(GADGETRON_SEQUENCEDESCRIPTION, GADGETRON_IMAGE_T1MAP);
            meta.append(GADGETRON_IMAGEPROCESSINGHISTORY, GADGETRON_IMAGE_MOCO);
            meta.append(GADGETRON_IMAGEPROCESSINGHISTORY, GADGETRON_IMAGE_T1MAP);

            meta.set(GADGETRON_IMAGE_SCALE_RATIO, scaling_factor);
            meta.set(GADGETRON_IMAGE_WINDOWCENTER, (long)(window_center * scaling_factor));
            meta.set(GADGETRON_IMAGE_WINDOWWIDTH, (long)(window_center * scaling_factor));
            meta.set(GADGETRON_IMAGE_COLORMAP, lut.c_str());

            meta.set(GADGETRON_IMAGECOMMENT, meta.as_str(GADGETRON_DATA_ROLE));
            meta.append(GADGETRON_IMAGECOMMENT, scalingStr.c_str());
            meta.append(GADGETRON_IMAGECOMMENT, unitStr.c_str());

            return std::move(meta);
        }

        struct T1Data {
            hoNDArray<std::complex<float>> data;
            std::vector<float> TI_values;
        };

        static std::vector<T1Data> extract_groups(const hoNDArray<ISMRMRD::AcquisitionHeader>& acq_headers, const hoNDArray<std::complex<float>>& data, const std::vector<float>& TI_values){


            std::vector<int> sets;
            for (auto subarray : spans(acq_headers, 1)){
                auto& header
                    = *std::find_if(subarray.begin(), subarray.end(), [](ISMRMRD::AcquisitionHeader& acq_header) {
                      return acq_header.isFlagSet(ISMRMRD::ISMRMRD_ACQ_LAST_IN_SLICE);
                    });
                sets.push_back(header.user_int[5]);
            }

            auto distinct_sets = std::set(sets.begin(),sets.end());

            std::vector<std::vector<int>> groups;
            for (auto s : distinct_sets){
                std::vector<int> group;
                for (int i = 0; i < sets.size(); i++){
                    if (sets[i] == s) group.push_back(i);
                }
                groups.push_back(std::move(group));
            }

            std::vector<T1Data> result;
            for (const auto& group : groups){
                using namespace Gadgetron::Indexing;
                auto group_dims = std::vector<size_t>(data.dimensions().begin(),data.dimensions().end()-1);
                group_dims.push_back(group.size());
                hoNDArray<std::complex<float>> group_data(group_dims);
                for (int i = 0; i < group.size(); i++){
                    group_data(slice,slice,i) = data(slice,slice,group[i]);
                }
                std::vector<float> group_TI;
                for (auto d : group) group_TI.push_back(TI_values[d]);
                result.push_back({std::move(group_data), std::move(group_TI)});

            }
            return result;


        }

        std::vector<hoNDArray<vector_td<float,2>>> group_t1_registration(const std::vector<T1Data>& t1data) const{
            using namespace ranges;
            auto vector_fields = t1data | views::transform([this](const T1Data& data){ return T1::t1_moco_cmr(data.data,data.TI_values, iterations); }) | to<std::vector>();

            return vector_fields;
        }


        auto register_group_and_compose(const T1Data& t1data, const hoNDArray<vector_td<float,2>>& vector_fields, const hoNDArray<std::complex<float>>& reference_frame){
            using namespace Gadgetron::Indexing;
            auto max_TI_index = std::max_element(t1data.TI_values.begin(),t1data.TI_values.end())-t1data.TI_values.begin();


            auto largest_TI_image = t1data(slice,slice,max_TI_index);

        }

        static hoNDArray<vector_td<float,2>> register_groups(const std::vector<T1Data>& t1data, const std::vector<hoNDArray<vector_td<float,2>>>& vector_fields){
            using namespace ranges;
            using namespace Gadgetron::Indexing;
            auto max_TIs = t1data | view::transform([](const auto& data){ return *std::max_element(data.TI_values.begin(),data.TI_values.end());}) | to<std::vector>();
                auto max_TI_group = std::max_element(begin(max_TIs),end(max_TIs)) - begin(max_TIs);

            const auto& [reference_data, reference_TIs] = t1data[max_TI_group];
            auto deformed_reference = T1::deform_groups(reference_data,vector_fields[max_TI_group]);
            auto max_TI_index = std::max_element(reference_TIs.begin(),reference_TIs.end())-reference_TIs.begin();
            const hoNDArray<std::complex<float>> reference_frame = deformed_reference(slice,slice,max_TI_index);


            auto result_dimensions = reference_data.dimensions();
            result_dimensions.back() = ranges::accumulate(t1data | view::transform([](const auto& data){return data.data.dimensions.back();}),0);
            hoNDArray<vector_td<float,2>> result;

            for (int i = 0; i < t1data.size(); i++ ){
                if (i == max_TI_group) continue;
            }


        }



        static std::vector<float> extract_MOLLI_TI(const hoNDArray<ISMRMRD::AcquisitionHeader>& acq_headers) {

            std::map<int, std::vector<ISMRMRD::AcquisitionHeader>> look_locker_sets;

            for (auto subarray : spans(acq_headers, 1)) {
                auto& header
                    = *std::find_if(subarray.begin(), subarray.end(), [](ISMRMRD::AcquisitionHeader& acq_header) {
                          return acq_header.isFlagSet(ISMRMRD::ISMRMRD_ACQ_LAST_IN_SLICE);
                      });
                look_locker_sets[header.user_int[5]].push_back(header);
            }

            std::vector<float> TI_values;

            for (auto& [set, headers] : look_locker_sets) {
                auto minimum_acquisition_time_stamp = std::accumulate(
                    headers.begin(), headers.end(), std::numeric_limits<uint32_t>::max(), [](auto val1, auto val2) {
                        return val1 < val2.acquisition_time_stamp ? val1 : val2.acquisition_time_stamp;
                    });

                for (auto& header : headers) {
                    float TI_value
                        = (header.acquisition_time_stamp - minimum_acquisition_time_stamp) * 2.5f + header.user_int[4];
                    TI_values.push_back(TI_value);
                    GDEBUG("set %d look-locker %d ti: %f  acq_time_stamp: %d  \n", header.idx.set, set, TI_value,
                        header.acquisition_time_stamp);
                }
            }

            return TI_values;
        }

        static void clean_image(hoNDArray<float>& data) {
            std::transform(data.begin(), data.end(), data.begin(), [](auto val) {
                if (val <= 0)
                    return 0.0f;
                if (val >= 5000)
                    return 0.0f;
                if (std::isnan(val))
                    return 0.0f;
                return val;
            });
        }

        void sort_images(IsmrmrdImageArray& images, const std::vector<float>& TI_values, std::vector<float>& TI_sorted ){
            auto sorted_index = argsort(TI_values);

            auto dims = images.data_.dimensions();
            images.data_.reshape(dims[0],dims[1],-1);
            auto data_copy = images.data_;
            std::fill(images.data_.begin(),images.data_.end(),std::complex<float>(1));

            for (size_t i = 0; i < data_copy.get_size(2); i++){
                using namespace Gadgetron::Indexing;
                images.data_(slice,slice,i) = data_copy(slice,slice,sorted_index[i]);
            }
            images.data_.reshape(dims);

            TI_sorted = TI_values;

            for (size_t i=0; i<TI_values.size(); i++)
                TI_sorted[i] = TI_values[ sorted_index[i] ];
        }
        void  set_RAW_headers_and_meta(IsmrmrdImageArray& images, const std::vector<float>& TI_values){
            for (auto& header : images.headers_ ) {
                header.image_type = ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE;
                header.image_series_index = 1;
            }

            for (size_t ind = 0; ind < images.meta_.size(); ind++){
                auto& meta = images.meta_[ind];
                meta.set(GADGETRON_IMAGE_INVERSIONTIME, (double)(TI_values[ind]));
            }
        }

        void  set_MOCO_MAG_headers_and_meta(IsmrmrdImageArray& images, const std::vector<float>& TI_values){
            for (auto& header : images.headers_ ) {
                header.image_type = ISMRMRD::ISMRMRD_IMTYPE_MAGNITUDE;
                header.image_series_index = 2;
            }

            for (size_t ind = 0; ind < images.meta_.size(); ind++){
                auto& meta = images.meta_[ind];
                meta.set(GADGETRON_DATA_ROLE,GADGETRON_IMAGE_MOCO);
                meta.set(GADGETRON_IMAGE_INVERSIONTIME, (double)(TI_values[ind]));
            }
        }

        void  set_PSIR_headers_and_meta(IsmrmrdImageArray& images, const std::vector<float>& TI_values){
            for (auto& header : images.headers_ ) {
                header.image_type = ISMRMRD::ISMRMRD_IMTYPE_REAL;
                header.image_series_index = 3;
            }


            images.data_ += 4096.0;
            for (size_t ind = 0; ind < images.meta_.size(); ind++){
                auto& meta = images.meta_[ind];
                meta.set(GADGETRON_DATA_ROLE,GADGETRON_IMAGE_PSIR);
                meta.append(GADGETRON_DATA_ROLE,GADGETRON_IMAGE_MOCO);
                meta.set(GADGETRON_IMAGE_SCALE_OFFSET, (double)(4096.0));
                meta.set(GADGETRON_IMAGE_INVERSIONTIME, (double)(TI_values[ind]));
            }
        }

        template <class T> std::vector<size_t> argsort(const std::vector<T>& data) {

            std::vector<std::pair<size_t, T>> index_and_values(data.size());

            for (size_t i = 0; i < data.size(); i++)
                index_and_values[i] = { i, data[i] };

            std::stable_sort(index_and_values.begin(), index_and_values.end(),
                [](auto val1, auto val2) { return val1.second < val2.second; });

            std::vector<size_t> index(data.size());

            for (size_t i = 0; i < data.size(); i++)
                index[i] = index_and_values[i].first;
            return index;
        }

        const std::vector<float> TIs;
        float field_strength;
    };

    GADGETRON_GADGET_EXPORT(T1MocoGadget)
}