// Copyright Epic Games, Inc. All Rights Reserved.

using System.Net;
using System.Net.Http;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Horde.Storage.Controllers;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;

namespace Horde.Storage.Implementation
{
    public class CacheContentIdStore : RelayStore, IContentIdStore
    {
        
        public CacheContentIdStore(IOptionsMonitor<UpstreamRelaySettings> settings, IHttpClientFactory httpClientFactory, IServiceCredentials serviceCredentials) : base(settings, httpClientFactory, serviceCredentials)
        {
        }

        public async Task<BlobIdentifier[]?> Resolve(NamespaceId ns, ContentId contentId, bool mustBeContentId)
        {
            HttpRequestMessage getContentIdRequest = BuildHttpRequest(HttpMethod.Get, $"api/v1/content-id/{ns}/{contentId}");
            HttpResponseMessage response = await HttpClient.SendAsync(getContentIdRequest);

            if (response.StatusCode == HttpStatusCode.NotFound)
                return null;

            response.EnsureSuccessStatusCode();
            ResolvedContentIdResponse resolvedContentId = await response.Content.ReadAsAsync<ResolvedContentIdResponse>();

            return resolvedContentId.Blobs;
        }

        public async Task Put(NamespaceId ns, ContentId contentId, BlobIdentifier blobIdentifier, int contentWeight)
        {
            HttpRequestMessage putContentIdRequest = BuildHttpRequest(HttpMethod.Put, $"api/v1/content-id/{ns}/{contentId}/update/{blobIdentifier}/{contentWeight}");
            HttpResponseMessage response = await HttpClient.SendAsync(putContentIdRequest);

            response.EnsureSuccessStatusCode();
        }
    }
}
