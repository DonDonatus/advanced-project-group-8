% mlp_latency_hiding_model.m
% CPEN 438 Project 8 -- "Ahead of the Storm"
% WEEK 3 VERSION -- illustrative placeholders replaced with measured data.
%
% Analytical model: predicts AMAT (Average Memory Access Time) as a
% function of PREFETCH DISTANCE, and compares that prediction against
% measured simulator data (analysis/distance_sweep.csv, seed = 8).
%
% Prefetch distance is realised in the simulator as STREAMBUF_DEPTH --
% the number of sequential lines the stream buffer runs ahead of the
% current access. Swept 1..12 via -DSTREAMBUF_DEPTH=<d>.
%
% Satisfies Requirements F10 (model provided) and F11 (validated
% against measured data).

clear; clc;

%% ---- Parameters -------------------------------------------------
memory_latency_cycles = 200;      % cycles to service a real miss
hit_time_cycles       = 4;        % cycles for a cache hit
distances             = 1:1:12;   % prefetch distances modelled

% Baseline miss rates MEASURED with prefetching disabled (mode = none,
% results.csv, seed 8). Previously a single illustrative 0.38.
base_miss_rate_stride_regular = 1 - 0.8750;   % = 0.1250
base_miss_rate_irregular      = 1 - 0.4440;   % = 0.5560

%% ---- Analytical AMAT model --------------------------------------
% A prefetch issued `distance` lines ahead has that many accesses'
% worth of time to complete. If distance * time_per_access >= latency,
% the miss is fully hidden; otherwise only a fraction is hidden.
time_per_access_cycles = hit_time_cycles;                 % simplifying assumption
fraction_hidden        = min(1, (distances * time_per_access_cycles) / memory_latency_cycles);
effective_miss_penalty = memory_latency_cycles * (1 - fraction_hidden);

amat_model_sr  = hit_time_cycles + base_miss_rate_stride_regular * effective_miss_penalty;
amat_model_irr = hit_time_cycles + base_miss_rate_irregular      * effective_miss_penalty;

%% ---- MEASURED simulator data (distance_sweep.csv, seed 8) -------
measured_distances = 1:1:12;

% Stream-buffer hit rate, stride_regular.trace
measured_hit_rate_sr = [0.9998 0.9998 0.9998 0.9998 0.9998 0.9998 ...
                        0.9998 0.9998 0.9998 0.9998 0.9998 0.9998];

% Stream-buffer hit rate, irregular.trace
measured_hit_rate_irr = [0.4445 0.4445 0.4445 0.4450 0.4455 0.4465 ...
                         0.4490 0.4605 0.4615 0.4640 0.4650 0.4660];

measured_amat_sr  = hit_time_cycles + (1 - measured_hit_rate_sr)  * memory_latency_cycles;
measured_amat_irr = hit_time_cycles + (1 - measured_hit_rate_irr) * memory_latency_cycles;

%% ---- Plot --------------------------------------------------------
figure;

subplot(1,2,1);
plot(distances, amat_model_sr, '-o', 'LineWidth', 1.5, 'DisplayName', 'Analytical model');
hold on;
plot(measured_distances, measured_amat_sr, 's--', 'MarkerSize', 8, ...
     'LineWidth', 1.5, 'DisplayName', 'Measured (simulator)');
xlabel('Prefetch distance (lines ahead)');
ylabel('AMAT (cycles)');
title('stride\_regular.trace');
legend('Location', 'northeast'); grid on;

subplot(1,2,2);
plot(distances, amat_model_irr, '-o', 'LineWidth', 1.5, 'DisplayName', 'Analytical model');
hold on;
plot(measured_distances, measured_amat_irr, 's--', 'MarkerSize', 8, ...
     'LineWidth', 1.5, 'DisplayName', 'Measured (simulator)');
xlabel('Prefetch distance (lines ahead)');
ylabel('AMAT (cycles)');
title('irregular.trace');
legend('Location', 'northeast'); grid on;

sgtitle('AMAT vs. Prefetch Distance: Analytical Model vs. Measured (seed 8)');

%% ---- Quantified model error -------------------------------------
err_sr  = measured_amat_sr  - amat_model_sr;
err_irr = measured_amat_irr - amat_model_irr;

fprintf('=== Model vs. measured, stride_regular ===\n');
fprintf('  model  AMAT d=1 / d=12 : %6.2f / %6.2f cycles\n', amat_model_sr(1), amat_model_sr(12));
fprintf('  measured AMAT d=1 / d=12 : %6.2f / %6.2f cycles\n', measured_amat_sr(1), measured_amat_sr(12));
fprintf('  mean abs error           : %6.2f cycles\n', mean(abs(err_sr)));

fprintf('\n=== Model vs. measured, irregular ===\n');
fprintf('  model  AMAT d=1 / d=12 : %6.2f / %6.2f cycles\n', amat_model_irr(1), amat_model_irr(12));
fprintf('  measured AMAT d=1 / d=12 : %6.2f / %6.2f cycles\n', measured_amat_irr(1), measured_amat_irr(12));
fprintf('  mean abs error           : %6.2f cycles\n', mean(abs(err_irr)));

fprintf('\nNOTE (Week 3 finding): the model assumes miss rate is FIXED and\n');
fprintf('only the penalty shrinks with distance. Measurement shows the\n');
fprintf('opposite on both traces -- see Week 3 report section 5.\n');
