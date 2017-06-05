// Copyright Alan (AJ) Pryor, Jr. 2017
// Transcribed from MATLAB code by Colin Ophus
// PRISM is distributed under the GNU General Public License (GPL)
// If you use PRISM, we ask that you cite the following papers:

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <numeric>
#include "configure.h"
#include <numeric>
#include "meta.h"
#include "ArrayND.h"
#include "params.h"
#include "utility.h"
#include "fftw3.h"
#include "WorkDispatcher.h"
#include "Multislice_calcOutput.h"
namespace PRISM{
	using namespace std;
	static const PRISM_FLOAT_PRECISION pi = acos(-1);
	static const std::complex<PRISM_FLOAT_PRECISION> i(0, 1);
	mutex fftw_plan_lock; // for synchronizing access to shared FFTW resources

	void setupCoordinates_multislice(Parameters<PRISM_FLOAT_PRECISION>& pars){

		// setup coordinates and build propagators
		Array1D<PRISM_FLOAT_PRECISION> xR = zeros_ND<1, PRISM_FLOAT_PRECISION>({{2}});
		xR[0] = pars.meta.scanWindowXMin * pars.tiledCellDim[2];
		xR[1] = pars.meta.scanWindowXMax * pars.tiledCellDim[2];
		Array1D<PRISM_FLOAT_PRECISION> yR = zeros_ND<1, PRISM_FLOAT_PRECISION>({{2}});
		yR[0] = pars.meta.scanWindowYMin * pars.tiledCellDim[1];
		yR[1] = pars.meta.scanWindowYMax * pars.tiledCellDim[1];

		vector<PRISM_FLOAT_PRECISION> xp_d = vecFromRange(xR[0], pars.meta.probe_stepX, xR[1]);
		vector<PRISM_FLOAT_PRECISION> yp_d = vecFromRange(yR[0], pars.meta.probe_stepY, yR[1]);

		Array1D<PRISM_FLOAT_PRECISION> xp(xp_d, {{xp_d.size()}});
		Array1D<PRISM_FLOAT_PRECISION> yp(yp_d, {{yp_d.size()}});
		pars.xp = xp;
		pars.yp = yp;
		pars.imageSize[0] = pars.pot.get_dimj();
		pars.imageSize[1] = pars.pot.get_dimi();
		Array1D<PRISM_FLOAT_PRECISION> qx = makeFourierCoords(pars.imageSize[1], pars.pixelSize[1]);
		Array1D<PRISM_FLOAT_PRECISION> qy = makeFourierCoords(pars.imageSize[0], pars.pixelSize[0]);

		pair< Array2D<PRISM_FLOAT_PRECISION>, Array2D<PRISM_FLOAT_PRECISION> > mesh = meshgrid(qy,qx);
		pars.qya = mesh.first;
		pars.qxa = mesh.second;
		Array2D<PRISM_FLOAT_PRECISION> q2(pars.qya);
		transform(pars.qxa.begin(), pars.qxa.end(),
		          pars.qya.begin(), q2.begin(), [](const PRISM_FLOAT_PRECISION& a, const PRISM_FLOAT_PRECISION& b){
					return a*a + b*b;
				});
		Array2D<PRISM_FLOAT_PRECISION> q1(q2);
		pars.q2 = q2;
		pars.q1 = q1;
		for (auto& q : pars.q1)q=sqrt(q);

		// get qMax
//		pars.qMax = 0;
//		{
//			PRISM_FLOAT_PRECISION qx_max;
//			PRISM_FLOAT_PRECISION qy_max;
//			for (auto ii = 0; ii < qx.size(); ++ii) {
//				qx_max = ( abs(qx[ii]) > qx_max) ? abs(qx[ii]) : qx_max;
//				qy_max = ( abs(qy[ii]) > qy_max) ? abs(qy[ii]) : qy_max;
//			}
//			pars.qMax = min(qx_max, qy_max) / 2;
//		}

		// get qMax
		long long ncx = (long long) floor((PRISM_FLOAT_PRECISION) pars.imageSize[1] / 2);
		PRISM_FLOAT_PRECISION dpx = 1.0 / ((PRISM_FLOAT_PRECISION)pars.imageSize[1] * pars.meta.realspace_pixelSize[1]);
		long long ncy = (long long) floor((PRISM_FLOAT_PRECISION) pars.imageSize[0] / 2);
		PRISM_FLOAT_PRECISION dpy = 1.0 / ((PRISM_FLOAT_PRECISION)pars.imageSize[0] * pars.meta.realspace_pixelSize[0]);
		pars.qMax = std::min(dpx*(ncx), dpy*(ncy)) / 2;


		pars.qMask = zeros_ND<2, unsigned int>({{pars.imageSize[0], pars.imageSize[1]}});
		{
			long offset_x = pars.qMask.get_dimi()/4;
			long offset_y = pars.qMask.get_dimj()/4;
			long ndimy = (long)pars.qMask.get_dimj();
			long ndimx = (long)pars.qMask.get_dimi();
			for (long y = 0; y < pars.qMask.get_dimj() / 2; ++y) {
				for (long x = 0; x < pars.qMask.get_dimi() / 2; ++x) {
					pars.qMask.at( ((y-offset_y) % ndimy + ndimy) % ndimy,
					               ((x-offset_x) % ndimx + ndimx) % ndimx) = 1;
				}
			}
		}

		// build propagators
		pars.prop     = zeros_ND<2, std::complex<PRISM_FLOAT_PRECISION> >({{pars.imageSize[0], pars.imageSize[1]}});
		pars.propBack = zeros_ND<2, std::complex<PRISM_FLOAT_PRECISION> >({{pars.imageSize[0], pars.imageSize[1]}});
		for (auto y = 0; y < pars.qMask.get_dimj(); ++y) {
			for (auto x = 0; x < pars.qMask.get_dimi(); ++x) {
				if (pars.qMask.at(y,x)==1)
				{
					pars.prop.at(y,x)     = exp(-i * pi * complex<PRISM_FLOAT_PRECISION>(pars.lambda, 0) *
					                            complex<PRISM_FLOAT_PRECISION>(pars.meta.sliceThickness, 0) *
					                            complex<PRISM_FLOAT_PRECISION>(pars.q2.at(y, x), 0));
					pars.propBack.at(y,x) = exp(i * pi * complex<PRISM_FLOAT_PRECISION>(pars.lambda, 0) *
					                            complex<PRISM_FLOAT_PRECISION>(pars.tiledCellDim[0], 0) *
					                            complex<PRISM_FLOAT_PRECISION>(pars.q2.at(y, x), 0));
				}
			}
		}

	}

	void setupDetector_multislice(Parameters<PRISM_FLOAT_PRECISION>& pars){
		pars.alphaMax = pars.qMax * pars.lambda;
		vector<PRISM_FLOAT_PRECISION> detectorAngles_d = vecFromRange(pars.meta.detector_angle_step / 2, pars.meta.detector_angle_step, pars.alphaMax - pars.meta.detector_angle_step / 2);
		Array1D<PRISM_FLOAT_PRECISION> detectorAngles(detectorAngles_d, {{detectorAngles_d.size()}});
		pars.detectorAngles = detectorAngles;
		pars.Ndet = pars.detectorAngles.size();
		Array2D<PRISM_FLOAT_PRECISION> alpha = pars.q1 * pars.lambda;
		pars.alphaInd = (alpha + pars.meta.detector_angle_step/2) / pars.meta.detector_angle_step;
		for (auto& q : pars.alphaInd) q = std::round(q);
		pars.dq = (pars.qxa.at(0, 1) + pars.qya.at(1, 0)) / 2;
	}

	void setupProbes_multislice(Parameters<PRISM_FLOAT_PRECISION>& pars){

		PRISM_FLOAT_PRECISION qProbeMax = pars.meta.probeSemiangle/ pars.lambda; // currently a single semiangle
		pars.psiProbeInit = zeros_ND<2, complex<PRISM_FLOAT_PRECISION> >({{pars.q1.get_dimj(), pars.q1.get_dimi()}});
		Array2D<complex<PRISM_FLOAT_PRECISION> > psi;
		transform(pars.psiProbeInit.begin(), pars.psiProbeInit.end(),
		          pars.q1.begin(), pars.psiProbeInit.begin(),
		          [&pars, &qProbeMax](std::complex<PRISM_FLOAT_PRECISION> &a, PRISM_FLOAT_PRECISION &q1_t) {
			          a.real(erf((qProbeMax - q1_t) / (0.5 * pars.dq)) * 0.5 + 0.5);
			          a.imag(0);
			          return a;
		          });

		transform(pars.psiProbeInit.begin(), pars.psiProbeInit.end(),
		          pars.q2.begin(), pars.psiProbeInit.begin(),
		          [&pars](std::complex<PRISM_FLOAT_PRECISION> &a, PRISM_FLOAT_PRECISION &q2_t) {
			          a = a * exp(-i * pi * pars.lambda * pars.meta.probeDefocus * q2_t); // TODO: fix hardcoded length-1 defocus
			          return a;
		          });
		PRISM_FLOAT_PRECISION norm_constant = sqrt(accumulate(pars.psiProbeInit.begin(), pars.psiProbeInit.end(),
		                                                      (PRISM_FLOAT_PRECISION)0.0, [](PRISM_FLOAT_PRECISION accum, std::complex<PRISM_FLOAT_PRECISION> &a) {
					return accum + abs(a) * abs(a);
				})); // make sure to initialize with 0.0 and NOT 0 or it won't be a float and answer will be wrong
		PRISM_FLOAT_PRECISION a = 0;
		for (auto &i : pars.psiProbeInit) { a += i.real(); };
		transform(pars.psiProbeInit.begin(), pars.psiProbeInit.end(),
		          pars.psiProbeInit.begin(), [&norm_constant](std::complex<PRISM_FLOAT_PRECISION> &a) {
					return a / norm_constant;
				});
	}

	void createTransmission(Parameters<PRISM_FLOAT_PRECISION>& pars){
		pars.transmission = zeros_ND<3, complex<PRISM_FLOAT_PRECISION> >(
				{{pars.pot.get_dimk(), pars.pot.get_dimj(), pars.pot.get_dimi()}});
		{
			auto p = pars.pot.begin();
			for (auto &j:pars.transmission)j = exp(i * pars.sigma * (*p++));
		}
	}

	void createStack(Parameters<PRISM_FLOAT_PRECISION>& pars){
		pars.output = zeros_ND<3, PRISM_FLOAT_PRECISION>({{pars.yp.size(), pars.xp.size(), pars.Ndet}});
	}

	void formatOutput_CPU_integrate(Parameters<PRISM_FLOAT_PRECISION>& pars,
	                                       Array2D< complex<PRISM_FLOAT_PRECISION> >& psi,
	                                       const Array2D<PRISM_FLOAT_PRECISION> &alphaInd,
	                                       const size_t ay,
	                                       const size_t ax){
		Array2D<PRISM_FLOAT_PRECISION> intOutput = zeros_ND<2, PRISM_FLOAT_PRECISION>({{psi.get_dimj(), psi.get_dimi()}});
		auto psi_ptr = psi.begin();
		for (auto& j:intOutput) j = pow(abs(*psi_ptr++),2);

		//save 4D output if applicable
		if (pars.meta.save4DOutput) {
			std::string section4DFilename = generateFilename(pars, ay, ax);
			intOutput.toMRC_f(section4DFilename.c_str());
		}

		//update stack -- ax,ay are unique per thread so this write is thread-safe without a lock
		auto idx = alphaInd.begin();
		for (auto counts = intOutput.begin(); counts != intOutput.end(); ++counts){
			if (*idx <= pars.Ndet){
				pars.output.at(ay,ax,(*idx)-1) += *counts;
			}
			++idx;
		};
	}
	void formatOutput_CPU_integrate_batch(Parameters<PRISM_FLOAT_PRECISION>& pars,
	                                      Array1D< complex<PRISM_FLOAT_PRECISION> >& psi_stack,
	                                      const Array2D<PRISM_FLOAT_PRECISION> &alphaInd,
	                                      size_t Nstart,
	                                      const size_t Nstop){
		int probe_idx = 0;
		while (Nstart < Nstop) {
			const size_t ay = Nstart / pars.xp.size();
			const size_t ax = Nstart % pars.xp.size();
			Array2D<PRISM_FLOAT_PRECISION> intOutput = zeros_ND<2, PRISM_FLOAT_PRECISION>(
					{{pars.psiProbeInit.get_dimj(), pars.psiProbeInit.get_dimi()}});
			auto psi_ptr = &psi_stack[probe_idx*pars.psiProbeInit.size()];
			for (auto &j:intOutput) j = pow(abs(*psi_ptr++), 2);

			//save 4D output if applicable
			if (pars.meta.save4DOutput) {
				std::string section4DFilename = generateFilename(pars, ay, ax);
				intOutput.toMRC_f(section4DFilename.c_str());
			}

			//update stack -- ax,ay are unique per thread so this write is thread-safe without a lock
			auto idx = alphaInd.begin();
			for (auto counts = intOutput.begin(); counts != intOutput.end(); ++counts) {
				if (*idx <= pars.Ndet) {
					pars.output.at(ay, ax, (*idx) - 1) += *counts;
				}
				++idx;
			};
			++Nstart;
			++probe_idx;
		}
	}

	std::pair<PRISM::Array2D< std::complex<PRISM_FLOAT_PRECISION> >, PRISM::Array2D< std::complex<PRISM_FLOAT_PRECISION> > >
	getSingleMultisliceProbe_CPU(Parameters<PRISM_FLOAT_PRECISION> &pars, const PRISM_FLOAT_PRECISION xp, const PRISM_FLOAT_PRECISION yp){

		Array2D< std::complex<PRISM_FLOAT_PRECISION> > realspace_probe;
		Array2D< std::complex<PRISM_FLOAT_PRECISION> > kspace_probe;
		PRISM_FFTW_INIT_THREADS();
		PRISM_FFTW_PLAN_WITH_NTHREADS(pars.meta.NUM_THREADS);
		Array2D<complex<PRISM_FLOAT_PRECISION> > psi(pars.psiProbeInit);
		unique_lock<mutex> gatekeeper(fftw_plan_lock);
		PRISM_FFTW_PLAN plan_forward = PRISM_FFTW_PLAN_DFT_2D(psi.get_dimj(), psi.get_dimi(),
		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
		                                                      FFTW_FORWARD, FFTW_ESTIMATE);
		PRISM_FFTW_PLAN plan_inverse = PRISM_FFTW_PLAN_DFT_2D(psi.get_dimj(), psi.get_dimi(),
		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
		                                                      FFTW_BACKWARD, FFTW_ESTIMATE);
		gatekeeper.unlock();
		{
			auto qxa_ptr = pars.qxa.begin();
			auto qya_ptr = pars.qya.begin();
			for (auto& p:psi)p*=exp(-2 * pi * i * ( (*qxa_ptr++)*xp +
			                                        (*qya_ptr++)*yp));
		}

		for (auto a2 = 0; a2 < pars.numPlanes; ++a2){
			PRISM_FFTW_EXECUTE(plan_inverse);
			complex<PRISM_FLOAT_PRECISION>* t_ptr = &pars.transmission[a2 * pars.transmission.get_dimj() * pars.transmission.get_dimi()];
			for (auto& p:psi)p *= (*t_ptr++); // transmit
			PRISM_FFTW_EXECUTE(plan_forward);
			auto p_ptr = pars.prop.begin();
			for (auto& p:psi)p *= (*p_ptr++); // propagate
			for (auto& p:psi)p /= psi.size(); // scale FFT
		}


		// output the region of the probe not masked by the anti-aliasing filter
		Array2D<complex<PRISM_FLOAT_PRECISION> > psi_small = zeros_ND<2, complex<PRISM_FLOAT_PRECISION> >({{psi.get_dimj()/2, psi.get_dimi()/2}});

		{
			long offset_x = psi.get_dimi() / 4;
			long offset_y = psi.get_dimj() / 4;
			long ndimy = (long) psi.get_dimj();
			long ndimx = (long)psi.get_dimi();
			for (long y = 0; y < psi.get_dimj() / 2; ++y) {
				for (long x = 0; x < psi.get_dimi() / 2; ++x) {
					psi_small.at(y, x) = psi.at(((y - offset_y) % ndimy + ndimy) % ndimy,
					                            ((x - offset_x) % ndimx + ndimx) % ndimx);
				}
			}
		}
		psi_small = fftshift2(psi_small);
		kspace_probe = psi_small;
		gatekeeper.lock();
		PRISM_FFTW_PLAN plan_inverse_small = PRISM_FFTW_PLAN_DFT_2D(psi_small.get_dimj(), psi_small.get_dimi(),
		                                                            reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_small[0]),
		                                                            reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_small[0]),
		                                                            FFTW_BACKWARD, FFTW_ESTIMATE);
		gatekeeper.unlock();
		PRISM_FFTW_EXECUTE(plan_inverse_small);
		realspace_probe = psi_small;

		// cleanup plans
		gatekeeper.lock();
		PRISM_FFTW_DESTROY_PLAN(plan_forward);
		PRISM_FFTW_DESTROY_PLAN(plan_inverse);
		PRISM_FFTW_DESTROY_PLAN(plan_inverse_small);
		PRISM_FFTW_CLEANUP_THREADS();
		return std::make_pair(realspace_probe, kspace_probe);
	};

	void getMultisliceProbe_CPU_batch(Parameters<PRISM_FLOAT_PRECISION>& pars,
	                                  const size_t Nstart,
	                                  const size_t Nstop,
	                                  PRISM_FFTW_PLAN& plan_forward,
	                                  PRISM_FFTW_PLAN& plan_inverse,
	                                  Array1D<complex<PRISM_FLOAT_PRECISION> >& psi_stack){
		{
			auto psi_ptr = psi_stack.begin();
			for (auto batch_num = 0; batch_num < min(pars.meta.batch_size_CPU, Nstop - Nstart); ++batch_num) {
				for (auto i:pars.psiProbeInit)*psi_ptr++ = i;
			}
		}
		auto psi_ptr   = psi_stack.begin();
		for (auto probe_num = Nstart; probe_num < Nstop; ++probe_num) {
//			for (auto i:pars.psiProbeInit)*psi_ptr++=i;
			// Initialize the probes
			// Determine x/y position from the linear index
			const size_t ay = probe_num / pars.xp.size();
			const size_t ax = probe_num % pars.xp.size();
			// populates the output stack for Multislice simulation using the CPU. The number of
			// threads used is determined by pars.meta.NUM_THREADS
			{
				auto qxa_ptr = pars.qxa.begin();
				auto qya_ptr = pars.qya.begin();
				for (auto jj = 0; jj < pars.qxa.size(); ++jj) {
					*psi_ptr++ *= exp(-2 * pi * i * ((*qxa_ptr++) * pars.xp[ax] +
					                               (*qya_ptr++) * pars.yp[ay]));
				}
			}
		}

		auto scaled_prop = pars.prop;
		for (auto& jj : scaled_prop) jj/=pars.psiProbeInit.size(); // apply FFT scaling factor here once in advance rather than at every plane
		complex<PRISM_FLOAT_PRECISION>* slice_ptr = &pars.transmission[0];
		for (auto a2 = 0; a2 < pars.numPlanes; ++a2){
			PRISM_FFTW_EXECUTE(plan_inverse); // batch FFT

			// transmit each of the probes in the batch
			for (auto batch_idx = 0; batch_idx < min(pars.meta.batch_size_CPU, Nstop - Nstart); ++batch_idx){
				auto t_ptr   = slice_ptr; // start at the beginning of the current slice
				auto psi_ptr = &psi_stack[batch_idx * pars.psiProbeInit.size()];
				for (auto jj = 0; jj < pars.psiProbeInit.size(); ++jj){
					*psi_ptr++ *= (*t_ptr++);// transmit
				}
			}
			slice_ptr += pars.psiProbeInit.size(); // advance to point to the beginning of the next potential slice
			PRISM_FFTW_EXECUTE(plan_forward); // batch FFT

			// propagate each of the probes in the batch
			for (auto batch_idx = 0; batch_idx < min(pars.meta.batch_size_CPU, Nstop - Nstart); ++batch_idx){
				auto p_ptr = scaled_prop.begin();
				auto psi_ptr = &psi_stack[batch_idx * pars.psiProbeInit.size()];
				for (auto jj = 0; jj < pars.psiProbeInit.size(); ++jj){
					*psi_ptr++ *= (*p_ptr++);// propagate
				}
			}

		}
		formatOutput_CPU_integrate_batch(pars, psi_stack, pars.alphaInd, Nstart, Nstop);
	}

	void getMultisliceProbe_CPU(Parameters<PRISM_FLOAT_PRECISION>& pars,
	                            const size_t ay,
	                            const size_t ax,
								PRISM_FFTW_PLAN& plan_forward,
								PRISM_FFTW_PLAN& plan_inverse,
								Array2D<complex<PRISM_FLOAT_PRECISION> >& psi){

		// populates the output stack for Multislice simulation using the CPU. The number of
		// threads used is determined by pars.meta.NUM_THREADS
//		Array2D<complex<PRISM_FLOAT_PRECISION> > psi(pars.psiProbeInit);
        psi = pars.psiProbeInit;
		// fftw_execute is the only thread-safe function in the library, so we need to synchronize access
		// to the plan creation methods
//		unique_lock<mutex> gatekeeper(fftw_plan_lock);
//		PRISM_FFTW_PLAN plan_forward = PRISM_FFTW_PLAN_DFT_2D(psi.get_dimj(), psi.get_dimi(),
//		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
//		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
//		                                                      FFTW_FORWARD, FFTW_ESTIMATE);
//		PRISM_FFTW_PLAN plan_inverse = PRISM_FFTW_PLAN_DFT_2D(psi.get_dimj(), psi.get_dimi(),
//		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
//		                                                      reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi[0]),
//		                                                      FFTW_BACKWARD, FFTW_ESTIMATE);
//		gatekeeper.unlock(); // unlock it so we only block as long as necessary to deal with plans
		{
			auto qxa_ptr = pars.qxa.begin();
			auto qya_ptr = pars.qya.begin();
			for (auto& p:psi)p*=exp(-2 * pi * i * ( (*qxa_ptr++)*pars.xp[ax] +
			                                        (*qya_ptr++)*pars.yp[ay]));
		}

		auto scaled_prop = pars.prop;
		for (auto& i : scaled_prop) i/=psi.size(); // apply FFT scaling factor here once in advance rather than at every plane
		complex<PRISM_FLOAT_PRECISION>* t_ptr = &pars.transmission[0];
		for (auto a2 = 0; a2 < pars.numPlanes; ++a2){
			PRISM_FFTW_EXECUTE(plan_inverse);
			for (auto& p:psi)p *= (*t_ptr++); // transmit
			PRISM_FFTW_EXECUTE(plan_forward);
			auto p_ptr = scaled_prop.begin();
			for (auto& p:psi)p *= (*p_ptr++); // propagate
		}
		formatOutput_CPU(pars, psi, pars.alphaInd, ay, ax);
	}

	void buildMultisliceOutput_CPUOnly(Parameters<PRISM_FLOAT_PRECISION>& pars){

#ifdef PRISM_BUILDING_GUI
        pars.progressbar->signalDescriptionMessage("Computing final output (Multislice)");
#endif

		vector<thread> workers;
		workers.reserve(pars.meta.NUM_THREADS); // prevents multiple reallocations
		PRISM_FFTW_INIT_THREADS();
		PRISM_FFTW_PLAN_WITH_NTHREADS(pars.meta.NUM_THREADS);
		const size_t PRISM_PRINT_FREQUENCY_PROBES = max((size_t)1,pars.xp.size() * pars.yp.size() / 10); // for printing status
		WorkDispatcher dispatcher(0, pars.xp.size() * pars.yp.size());

		// If the batch size is too big, the work won't be spread over the threads, which will usually hurt more than the benefit
		// of batch FFT
		pars.meta.batch_size_CPU = min(pars.meta.batch_size_target_CPU, max((size_t)1, pars.xp.size() * pars.yp.size() / pars.meta.NUM_THREADS));
		for (auto t = 0; t < pars.meta.NUM_THREADS; ++t){
			cout << "Launching CPU worker #" << t << endl;
			workers.push_back(thread([&pars, &dispatcher, t, &PRISM_PRINT_FREQUENCY_PROBES]() {
				size_t Nstart, Nstop;
                Nstart=Nstop=0;
				if (dispatcher.getWork(Nstart, Nstop, pars.meta.batch_size_CPU)){ // synchronously get work assignment

					// Allocate memory for the propagated probes. These are 2D arrays, but as they will be operated on
					// as a batch FFT they are all stacked together into one linearized array
					Array1D<complex<PRISM_FLOAT_PRECISION> > psi_stack = zeros_ND<1, complex<PRISM_FLOAT_PRECISION> >({{pars.psiProbeInit.size() * pars.meta.batch_size_CPU}});

					// setup batch FFTW parameters
					const int rank    = 2;
					int n[]           = {(int)pars.psiProbeInit.get_dimj(), (int)pars.psiProbeInit.get_dimi()};
					const int howmany = pars.meta.batch_size_CPU;
					int idist         = n[0]*n[1];
					int odist         = n[0]*n[1];
					int istride       = 1;
					int ostride       = 1;
					int *inembed      = n;
					int *onembed      = n;
					unique_lock<mutex> gatekeeper(fftw_plan_lock);

//					PRISM_FFTW_PLAN plan_forward = PRISM_FFTW_PLAN_DFT_2D(psi_stack.get_dimj(), psi_stack.get_dimi(),
//																		  reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]),
//																		  reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]),
//																		  FFTW_FORWARD, FFTW_MEASURE);
//					PRISM_FFTW_PLAN plan_inverse = PRISM_FFTW_PLAN_DFT_2D(psi_stack.get_dimj(), psi_stack.get_dimi(),
//																		  reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]),
//																		  reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]),
//																		  FFTW_BACKWARD, FFTW_MEASURE);


					PRISM_FFTW_PLAN plan_forward = PRISM_FFTW_PLAN_DFT_BATCH(rank, n, howmany,
					                                                         reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]), inembed,
					                                                         istride, idist,
					                                                         reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]), onembed,
					                                                         ostride, odist,
					                                                         FFTW_FORWARD, FFTW_MEASURE);
					PRISM_FFTW_PLAN plan_inverse = PRISM_FFTW_PLAN_DFT_BATCH(rank, n, howmany,
					                                                         reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]), inembed,
					                                                         istride, idist,
					                                                         reinterpret_cast<PRISM_FFTW_COMPLEX *>(&psi_stack[0]), onembed,
					                                                         ostride, odist,
					                                                         FFTW_BACKWARD, FFTW_MEASURE);

					gatekeeper.unlock();
					// main work loop
                    do {
						while (Nstart < Nstop) {
							if (Nstart % PRISM_PRINT_FREQUENCY_PROBES < pars.meta.batch_size_CPU | Nstart == 100){
								cout << "Computing Probe Position #" << Nstart << "/" << pars.xp.size() * pars.yp.size() << endl;
							}
//							getMultisliceProbe_CPU_batch(pars, Nstart, Nstop, pars.xp.size(), plan_forward, plan_inverse, psi);
							getMultisliceProbe_CPU_batch(pars, Nstart, Nstop, plan_forward, plan_inverse, psi_stack);
#ifdef PRISM_BUILDING_GUI
                            pars.progressbar->signalOutputUpdate(Nstart, pars.xp.size() * pars.yp.size());
#endif
							Nstart=Nstop;
						}
					} while(dispatcher.getWork(Nstart, Nstop, pars.meta.batch_size_CPU));
					gatekeeper.lock();
					PRISM_FFTW_DESTROY_PLAN(plan_forward);
					PRISM_FFTW_DESTROY_PLAN(plan_inverse);
					gatekeeper.unlock();
				}
				cout << "CPU worker #" << t << " finished\n";
			}));
		}
		for (auto& t:workers)t.join();
		PRISM_FFTW_CLEANUP_THREADS();
	};


	void Multislice_calcOutput(Parameters<PRISM_FLOAT_PRECISION>& pars){

		// setup coordinates and build propagators
		setupCoordinates_multislice(pars);

		// setup detector coordinates and angles
		setupDetector_multislice(pars);

		// create initial probes
		setupProbes_multislice(pars);

		// create transmission array
		createTransmission(pars);

		// initialize output stack
		createStack(pars);

		// create the output
		buildMultisliceOutput(pars);
	}
}