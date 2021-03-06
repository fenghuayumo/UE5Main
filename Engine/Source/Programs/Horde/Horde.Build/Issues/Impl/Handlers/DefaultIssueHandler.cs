// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using Horde.Build.Collections;
using Horde.Build.Models;

namespace Horde.Build.IssueHandlers.Impl
{
	/// <summary>
	/// Instance of a particular compile error
	/// </summary>
	class DefaultIssueHandler : IIssueHandler
	{
		/// <summary>
		/// Name of the handler
		/// </summary>
		public const string Type = "Default";

		/// <inheritdoc/>
		string IIssueHandler.Type => Type;

		/// <inheritdoc/>
		public int Priority => 0;

		/// <inheritdoc/>
		public bool TryGetFingerprint(IJob job, INode node, ILogEventData eventData, [NotNullWhen(true)] out NewIssueFingerprint? fingerprint)
		{
			fingerprint = new NewIssueFingerprint(Type, new[] { node.Name }, null);
			return true;
		}

		/// <inheritdoc/>
		public void RankSuspects(IIssueFingerprint fingerprint, List<SuspectChange> suspects)
		{
		}

		/// <inheritdoc/>
		public string GetSummary(IIssueFingerprint fingerprint, IssueSeverity severity)
		{
			string nodeName = fingerprint.Keys.FirstOrDefault() ?? "(unknown)";
			if(severity == IssueSeverity.Warning)
			{
				return $"Warnings in {nodeName}";
			}
			else
			{
				return $"Errors in {nodeName}";
			}
		}
	}
}
