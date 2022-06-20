// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Net.Http;
using Microsoft.Extensions.Options;

namespace Horde.Storage.Implementation
{
    public abstract class RelayStore
    {
        private readonly IOptionsMonitor<UpstreamRelaySettings> _settings;
        private readonly IServiceCredentials _serviceCredentials;
        private readonly HttpClient _httpClient;

        protected HttpClient HttpClient
        {
            get { return _httpClient; }
        }

        protected RelayStore(IOptionsMonitor<UpstreamRelaySettings> settings, IHttpClientFactory httpClientFactory, IServiceCredentials serviceCredentials)
        {
            _settings = settings;
            _serviceCredentials = serviceCredentials;

            _httpClient = httpClientFactory.CreateClient();
            _httpClient.BaseAddress = new Uri(_settings.CurrentValue.ConnectionString);
        }

        protected HttpRequestMessage BuildHttpRequest(HttpMethod method, string uri)
        {
            string? token = _serviceCredentials.GetToken();
            HttpRequestMessage request = new HttpRequestMessage(method, uri);
            if (!string.IsNullOrEmpty(token))
                request.Headers.Add("Authorization", "Bearer " + token);

            return request;
        }

    }
}