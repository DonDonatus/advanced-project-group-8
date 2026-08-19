% mlp_latency_hiding_model.m
% CPEN 438 Project 8 -- "Ahead of the Storm"
%
% Analytical model: predicts AMAT (Average Memory Access Time) as a
% function of PREFETCH DISTANCE -- how many lines ahead of the current
% access the prefetcher tries to stay -- and compares that prediction
% against your measured simulator data.
%
% The idea: a prefetch issued "distance" lines ahead has that many
% extra accesses' worth of time to complete before it's actually
% needed. If (distance x time_per_access) >= memory_latency, the
% prefetch is fully "timely" and completely hides the latency. If it's
% shorter, only part of the latency gets hidden.
%
% Fill in MEASURED_HIT_RATE below with your Week 3 simulator results
% (from results.csv) once you have them -- this file runs standalone
% with illustrative numbers until then.

clear; clc;

%% ---- Parameters (edit these to match your actual measurements) ----
memory_latency_cycles = 200;      % cycles to service a real miss
cycle_time_ns         = 1.0;      % ns per cycle (illustrative)
hit_time_cycles       = 4;        % cycles for a cache hit
base_miss_rate        = 0.38;     % miss rate with NO prefetching (from results.csv)

distances = 1:1:12;               % prefetch distances to model

%% ---- Analytical AMAT model ----
% Fraction of the miss latency that gets hidden by a given distance,
% assuming roughly constant time per access:
time_per_access_cycles = hit_time_cycles;   % simplifying assumption
fraction_hidden = min(1, (distances * time_per_access_cycles) / memory_latency_cycles);

% Effective penalty per (still-a-miss) access after partial hiding:
effective_miss_penalty = memory_latency_cycles * (1 - fraction_hidden);

% AMAT = hit_time + miss_rate * effective_miss_penalty
% (miss_rate itself would ideally also fall as distance increases and
%  timeliness improves -- for this simplified model we hold it fixed
%  and isolate the effect of hiding penalty vs. reducing miss count;
%  extend this if your report wants the combined effect.)
amat_model = hit_time_cycles + base_miss_rate * effective_miss_penalty;

%% ---- Plug in your measured simulator hit rates here ----
% Example placeholder -- REPLACE with your actual Week 3 numbers,
% one hit rate per distance you actually tested in the simulator.
measured_distances = [1 2 4 8];
measured_hit_rate  = [0.66 0.70 0.74 0.75];   % <-- replace with real data
measured_amat = hit_time_cycles + (1 - measured_hit_rate) * memory_latency_cycles;

%% ---- Plot ----
figure;
plot(distances, amat_model, '-o', 'LineWidth', 1.5, 'DisplayName', 'Analytical model');
hold on;
plot(measured_distances, measured_amat, 's', 'MarkerSize', 8, ...
     'LineWidth', 1.5, 'DisplayName', 'Measured (simulator)');
xlabel('Prefetch distance (lines ahead)');
ylabel('AMAT (cycles)');
title('AMAT vs. Prefetch Distance: Analytical Model vs. Measured');
legend('Location', 'northeast');
grid on;

fprintf('Analytical AMAT at distance=1: %.2f cycles\n', amat_model(1));
fprintf('Analytical AMAT at distance=8: %.2f cycles\n', amat_model(8));
fprintf('\nReplace measured_distances / measured_hit_rate with your own\n');
fprintf('results.csv numbers before submitting the Week 3 plot.\n');
